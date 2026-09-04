/* Copyright (C) 2024 John Törnblom

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#include <onion/log.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/mman.h>

#include <ps5/kernel.h>
#include <ps5/klog.h>

#include <onion/pt.h>


#ifndef PSL_C
#define PSL_C 0x00000001UL
#endif


/*
 * Private stack window for PT_SETREGS "calls".
 *
 * Hijacking a live ShellUI thread mid-frame (often SwapBuffers / Mono JIT)
 * must not reuse that frame's RSP:
 *   - x86-64 SysV red zone is [rsp-128, rsp); callee pushes would corrupt it
 *   - ABI requires rsp % 16 == 8 at function entry (as if a CALL just ran)
 *
 * Subtract 0x108 from the 16-byte-aligned interrupted rsp:
 *   0x108 > 128 (clears red zone) and is 8 mod 16 (correct entry alignment).
 * bak_reg is always restored after the remote work completes.
 *
 * Do not plant executable traps on this window: PS5 stack pages are NX, and
 * the callee's own frame would overwrite anything stored below entry_rsp.
 */
static uintptr_t
pt_private_entry_rsp(uintptr_t interrupted_rsp) {
  return (interrupted_rsp & ~(uintptr_t)0xf) - (uintptr_t)0x108;
}


static size_t
pt_page_size(void) {
  const long n = sysconf(_SC_PAGESIZE);
  return n > 0 ? (size_t)n : (size_t)0x4000;
}


/*
 * One RWX INT3 page per traced pid, used as the RET target for pt_call.
 * Allocated with the fast pt_syscall path (single-step of `syscall`).
 */
static intptr_t
pt_trap_gadget(pid_t pid) {
  static pid_t cached_pid = 0;
  static intptr_t cached_addr = 0;
  const size_t page_size = pt_page_size();
  intptr_t page;
  const uint8_t int3 = 0xcc;

  if (cached_pid == pid && cached_addr) {
    return cached_addr;
  }

  page = pt_mmap(pid, 0, page_size, PROT_READ | PROT_WRITE,
                 MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (!page || page == (intptr_t)-1) {
    return 0;
  }
  if (pt_copyin(pid, &int3, page, sizeof(int3))) {
    return 0;
  }
  if (kernel_mprotect(pid, page, page_size,
                      PROT_READ | PROT_WRITE | PROT_EXEC)) {
    LOG_ERROR("[DEBUG-PT] trap gadget mprotect failed pid=%d addr=%#lx",
              pid, (unsigned long)page);
    return 0;
  }

  cached_pid = pid;
  cached_addr = page;
  return page;
}


static int
pt_wait_sigtrap(pid_t pid, const char *what) {
  int status = 0;

  if (waitpid(pid, &status, 0) == -1) {
    return -1;
  }
  if (!WIFSTOPPED(status) || WSTOPSIG(status) != SIGTRAP) {
    LOG_ERROR("[DEBUG-PT] unexpected %s wait status=0x%x stopped=%d sig=%d",
              what, status, WIFSTOPPED(status),
              WIFSTOPPED(status) ? WSTOPSIG(status) : -1);
    errno = EPROTO;
    return -1;
  }
  return 0;
}


/*
 * Continue until the remote function RETs into the planted INT3 gadget.
 * jmp_reg must already have RIP/RSP/arguments filled; RSP is the private
 * entry window. On a valid SIGTRAP, bak_reg is restored and rax is returned.
 * Any other stop fails closed without restoring bak_reg.
 */
static long
pt_run_until_int3_return(pid_t pid, struct reg *jmp_reg, struct reg *bak_reg) {
  const intptr_t gadget = pt_trap_gadget(pid);
  const uintptr_t entry_rsp = (uintptr_t)jmp_reg->r_rsp;
  uintptr_t rip;

  if (!gadget) {
    errno = ENOMEM;
    return -1;
  }
  if (pt_setlong(pid, (intptr_t)entry_rsp, (long)gadget)) {
    return -1;
  }
  if (pt_setregs(pid, jmp_reg)) {
    return -1;
  }
  if (pt_continue(pid, 0) != 0) {
    return -1;
  }
  if (pt_wait_sigtrap(pid, "call") != 0) {
    return -1;
  }
  if (pt_getregs(pid, jmp_reg)) {
    return -1;
  }

  rip = (uintptr_t)jmp_reg->r_rip;
  if (rip != (uintptr_t)gadget && rip != (uintptr_t)gadget + 1) {
    LOG_ERROR("[DEBUG-PT] trap rip=%#lx not at planted int3 %#lx",
              (unsigned long)rip, (unsigned long)gadget);
    errno = EPROTO;
    return -1;
  }
  if (pt_setregs(pid, bak_reg)) {
    return -1;
  }
  return jmp_reg->r_rax;
}


static int
sys_ptrace(int request, pid_t pid, caddr_t addr, int data) {
  /*
   * Do NOT flip ucred authid around every ptrace syscall.
   *
   * Caller elevates once with set_ucred_to_ptrace() (PTRACE_AUTHID) for the
   * inject window, then restores. No per-call authid flip here.
   */
  return (int)syscall(SYS_ptrace, request, pid, addr, data);
}


intptr_t
pt_resolve(pid_t pid, const char* nid) {
  intptr_t addr;

  if((addr=kernel_dynlib_resolve(pid, 0x1, nid))) {
    return addr;
  }

  return kernel_dynlib_resolve(pid, 0x2001, nid);
}


int
pt_attach(pid_t pid) {
  int status = 0;

  if (sys_ptrace(PT_ATTACH, pid, 0, 0) == -1) {
    return -1;
  }

  if (waitpid(pid, &status, WUNTRACED) == -1) {
    return -1;
  }

  /* PT_ATTACH must leave the traced process stopped before callers touch
   * registers or its address space.  A plain waitpid success is not enough:
   * an exit/continued event must never be treated as the attach stop. */
  if (!WIFSTOPPED(status)) {
    errno = ESRCH;
    return -1;
  }

  return 0;
}


int
pt_detach(pid_t pid, int sig) {
  if(sys_ptrace(PT_DETACH, pid, 0, sig) == -1) {
    return -1;
  }

  return 0;
}


int
pt_step(int pid) {
  if(sys_ptrace(PT_STEP, pid, (caddr_t)1, 0)) {
    return -1;
  }

  if(waitpid(pid, 0, 0) < 0) {
    return -1;
  }

  return 0;
}


int
pt_continue(pid_t pid, int sig) {
  if(sys_ptrace(PT_CONTINUE, pid, (caddr_t)1, sig) == -1) {
    return -1;
  }

  return 0;
}


int
pt_getint(pid_t pid, intptr_t addr) {
  return sys_ptrace(PT_READ_D, pid, (caddr_t)addr, 0);
}


int
pt_setint(pid_t pid, intptr_t addr, int val) {
  return sys_ptrace(PT_WRITE_D, pid, (caddr_t)addr, val);
}


int
pt_getregs(pid_t pid, struct reg *r) {
  return sys_ptrace(PT_GETREGS, pid, (caddr_t)r, 0);
}


int
pt_setregs(pid_t pid, const struct reg *r) {
  return sys_ptrace(PT_SETREGS, pid, (caddr_t)r, 0);
}


int
pt_copyin(pid_t pid, const void* buf, intptr_t addr, size_t len) {
  struct ptrace_io_desc iod = {
    .piod_op = PIOD_WRITE_D,
    .piod_offs = (void*)addr,
    .piod_addr = (void*)buf,
    .piod_len = len};
  return sys_ptrace(PT_IO, pid, (caddr_t)&iod, 0);
}


int
pt_setchar(pid_t pid, intptr_t addr, char val) {
  return pt_copyin(pid, &val, addr, sizeof(val));
}


int
pt_setshort(pid_t pid, intptr_t addr, short val) {
  return pt_copyin(pid, &val, addr, sizeof(val));
}


int
pt_setlong(pid_t pid, intptr_t addr, long val) {
  return pt_copyin(pid, &val, addr, sizeof(val));
}


int
pt_copyout(pid_t pid, intptr_t addr, void* buf, size_t len) {
  struct ptrace_io_desc iod = {
    .piod_op = PIOD_READ_D,
    .piod_offs = (void*)addr,
    .piod_addr = buf,
    .piod_len = len};
  return sys_ptrace(PT_IO, pid, (caddr_t)&iod, 0);
}


char
pt_getchar(pid_t pid, intptr_t addr) {
  char val = 0;

  pt_copyout(pid, addr, &val, sizeof(val));

  return val;
}


short
pt_getshort(pid_t pid, intptr_t addr) {
  short val = 0;

  pt_copyout(pid, addr, &val, sizeof(val));

  return val;
}


long
pt_getlong(pid_t pid, intptr_t addr) {
  long val = 0;

  pt_copyout(pid, addr, &val, sizeof(val));

  return val;
}


long
pt_call(pid_t pid, intptr_t addr, ...) {
  struct reg jmp_reg;
  struct reg bak_reg;
  va_list ap;

  if(pt_getregs(pid, &bak_reg)) {
    return -1;
  }

  memcpy(&jmp_reg, &bak_reg, sizeof(jmp_reg));
  jmp_reg.r_rip = addr;
  jmp_reg.r_rsp = pt_private_entry_rsp((uintptr_t)bak_reg.r_rsp);

  va_start(ap, addr);
  jmp_reg.r_rdi = va_arg(ap, uint64_t);
  jmp_reg.r_rsi = va_arg(ap, uint64_t);
  jmp_reg.r_rdx = va_arg(ap, uint64_t);
  jmp_reg.r_rcx = va_arg(ap, uint64_t);
  jmp_reg.r_r8  = va_arg(ap, uint64_t);
  jmp_reg.r_r9  = va_arg(ap, uint64_t);
  va_end(ap);

  return pt_run_until_int3_return(pid, &jmp_reg, &bak_reg);
}

long 
pt_call2(pid_t pid, intptr_t addr, ...)
{
  struct reg jmp_reg;
  struct reg bak_reg;
  uintptr_t entry_rsp;
  va_list ap;

  if(pt_getregs(pid, &bak_reg)) {
    return -1;
  }

  memcpy(&jmp_reg, &bak_reg, sizeof(jmp_reg));
  jmp_reg.r_rip = addr;

  /*
   * Stager path (pthread_create + int3).  Same private stack rules as pt_call:
   * the interrupted ShellUI frame (often SceShellUIMain mid-SwapBuffers) must
   * not share rsp with the stager or its red zone is clobbered before bak_reg
   * is restored.
   */
  entry_rsp = pt_private_entry_rsp((uintptr_t)bak_reg.r_rsp);
  jmp_reg.r_rsp = entry_rsp;

  va_start(ap, addr);
  jmp_reg.r_rdi = va_arg(ap, uint64_t);
  jmp_reg.r_rsi = va_arg(ap, uint64_t);
  jmp_reg.r_rdx = va_arg(ap, uint64_t);
  jmp_reg.r_rcx = va_arg(ap, uint64_t);
  jmp_reg.r_r8  = va_arg(ap, uint64_t);
  jmp_reg.r_r9  = va_arg(ap, uint64_t);
  va_end(ap);

  if(pt_setregs(pid, &jmp_reg)) {
    return -1;
  }

  //
  // Continue until hit a breakpoint, that must be generated by the called function
  //
  /* The signal argument is delivered to the tracee.  SIGCONT is not a
   * harmless "continue" flag here; injecting it can wake unrelated ShellUI
   * waiters while the original thread is being repurposed as the stager.
   */
  if (pt_continue(pid, 0) != 0) {
    return -1;
  }

  /*
   * CRITICAL (kylin-core): wait for the target to actually stop on int3 before
   * restoring registers. Without waitpid, bak_reg is restored while bootstrap
   * is still running inside SceShellUI → crash / freeze / hard reboot.
   * Only the stager's terminal INT3 is a valid synchronization point.
   */
  if (pt_wait_sigtrap(pid, "stager") != 0) {
    return -1;
  }

  if (pt_setregs(pid, &bak_reg) != 0) {
    return -1;
  }

  return jmp_reg.r_rax;
}


long
pt_syscall(pid_t pid, int sysno, ...) {
  intptr_t addr = pt_resolve(pid, "HoLVWNanBBc");
  struct reg jmp_reg;
  struct reg bak_reg;
  va_list ap;
  uint8_t insn[2] = {0, 0};

  if(!addr) {
    return -1;
  }
  addr += 0xa;

  if(pt_getregs(pid, &bak_reg)) {
    return -1;
  }

  memcpy(&jmp_reg, &bak_reg, sizeof(jmp_reg));
  jmp_reg.r_rip = addr;
  jmp_reg.r_rax = sysno;

  /*
   * elfldr_load issues many remote mmap/mprotect/msync calls via this path
   * while SceShellUIMain may be stopped inside SwapBuffers.  Reusing that
   * rsp clobbers the red zone and yields Mono "instruction pointer is NULL"
   * crashes on resume — before any ShellUI payload hook runs.
   */
  jmp_reg.r_rsp = pt_private_entry_rsp((uintptr_t)bak_reg.r_rsp);

  va_start(ap, sysno);
  jmp_reg.r_rdi = va_arg(ap, uint64_t);
  jmp_reg.r_rsi = va_arg(ap, uint64_t);
  jmp_reg.r_rdx = va_arg(ap, uint64_t);
  jmp_reg.r_r10 = va_arg(ap, uint64_t);
  jmp_reg.r_r8  = va_arg(ap, uint64_t);
  jmp_reg.r_r9  = va_arg(ap, uint64_t);
  va_end(ap);

  /*
   * HoLVWNanBBc+0xa is the `syscall` opcode (0f 05). One PT_STEP runs the
   * whole kernel operation; the libc stub's jc/errno tail is not executed.
   * Translate FreeBSD CF→error the same way libc would, so callers that
   * compare against -1 keep working. Do not PT_CONTINUE here: other ShellUI
   * threads must stay stopped while the remote mmap/mprotect happens.
   */
  /* xotext may refuse the 2-byte probe; the +0xa convention still holds. */
  if (pt_copyout(pid, addr, insn, sizeof(insn)) == 0 &&
      (insn[0] != 0x0f || insn[1] != 0x05)) {
    LOG_ERROR("[DEBUG-PT] syscall stub+0xa is not syscall pid=%d addr=%#lx bytes=%02x %02x",
              pid, (unsigned long)addr, insn[0], insn[1]);
    errno = EPROTO;
    return -1;
  }

  if(pt_setregs(pid, &jmp_reg)) {
    return -1;
  }
  if(sys_ptrace(PT_STEP, pid, (caddr_t)1, 0) ||
     pt_wait_sigtrap(pid, "syscall") != 0 ||
     pt_getregs(pid, &jmp_reg)) {
    (void)pt_setregs(pid, &bak_reg);
    return -1;
  }
  if(pt_setregs(pid, &bak_reg)) {
    return -1;
  }

  if (jmp_reg.r_rflags & PSL_C) {
    errno = (int)jmp_reg.r_rax;
    return -1;
  }
  return jmp_reg.r_rax;
}


intptr_t
pt_mmap(pid_t pid, intptr_t addr, size_t len, int prot, int flags,
	int fd, off_t off) {
  return pt_syscall(pid, SYS_mmap, addr, len, prot, flags, fd, off);
}


int
pt_msync(pid_t pid, intptr_t addr, size_t len, int flags) {
  return pt_syscall(pid, SYS_msync, addr, len, flags);
}


int
pt_munmap(pid_t pid, intptr_t addr, size_t len) {
  return pt_syscall(pid, SYS_munmap, addr, len);
}


int
pt_mprotect(pid_t pid, intptr_t addr, size_t len, int prot) {
  return pt_syscall(pid, SYS_mprotect, addr, len, prot);
}


int
pt_socket(pid_t pid, int domain, int type, int protocol) {
  return (int)pt_syscall(pid, SYS_socket, domain, type, protocol);
}


int
pt_setsockopt(pid_t pid, int fd, int level, int optname, intptr_t optval,
	      socklen_t optlen) {
  return (int)pt_syscall(pid, SYS_setsockopt, fd, level, optname, optval,
			 optlen, 0);
}


int
pt_close(pid_t pid, int fd) {
  return (int)pt_syscall(pid, SYS_close, fd);
}


int
pt_bind(pid_t pid, int sockfd, intptr_t addr, uint32_t addrlen) {
  return (int)pt_syscall(pid, SYS_bind, sockfd, addr, addrlen);
}


ssize_t
pt_recvmsg(pid_t pid, int fd, intptr_t msg, int flags) {
  return (int)pt_syscall(pid, SYS_recvmsg, fd, msg, flags);
}


int
pt_dup2(pid_t pid, int oldfd, int newfd) {
  return (int)pt_syscall(pid, SYS_dup2, oldfd, newfd);
}


int
pt_rdup(pid_t pid, pid_t other_pid, int fd) {
  return (int)pt_syscall(pid, 0x25b, other_pid, fd);
}


int
pt_pipe(pid_t pid, intptr_t pipefd) {
  intptr_t faddr = pt_resolve(pid, "-Jp7F+pXxNg");
  return (int)pt_call(pid, faddr, pipefd);
}


int
pt_errno(pid_t pid) {
  intptr_t faddr = pt_resolve(pid, "9BcDykPmo1I");
  intptr_t addr = pt_call(pid, faddr);
  return pt_getint(pid, addr);
}


intptr_t
pt_sceKernelGetProcParam(pid_t pid) {
  intptr_t faddr = pt_resolve(pid, "959qrazPIrg");

  return pt_call(pid, faddr);
}
