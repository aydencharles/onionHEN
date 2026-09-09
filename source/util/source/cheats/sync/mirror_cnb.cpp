#include "cheats/sync/i_cheat_mirror.hpp"

#include <memory>

namespace onion::cheats::sync {
namespace {

class CnbCoolMirror final : public ICheatMirror {
public:
  CheatMirrorId id() const override { return CheatMirrorId::Cnb; }
  const char *name() const override { return "cnb"; }
  const char *archiveHost() const override { return "cnb.cool"; }
  std::string archiveUrl(const ICheatCatalog &catalog) const override {
    std::string url = "https://cnb.cool/";
    url += catalog.slugFor(id());
    url += "/-/git/archive/refs/heads/";
    url += "chinese-build";
    url += ".zip";
    return url;
  }
};

} // namespace

std::unique_ptr<ICheatMirror> make_cnb_mirror() {
  return std::unique_ptr<ICheatMirror>(new CnbCoolMirror());
}

} // namespace onion::cheats::sync
