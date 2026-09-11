#include "cheats/sync/i_cheat_catalog.hpp"

#include <cstddef>

namespace onion::cheats::sync {
namespace {

/**
 * Only translation unit allowed to name the community collection.
 * Sync / mirrors / git never include this file's internals.
 */
class HenCheatsCollection final : public ICheatCatalog {
public:
  const char *id() const override { return "hen-cheats-collection"; }
  const char *defaultBranch() const override { return "master"; }

  const char *slugFor(CheatMirrorId mirror) const override {
    if (mirror == CheatMirrorId::Cnb) {
      return "PS5-xiexiyu/CN-Cheats-Collection";
    }
    return "TeeKay87/HEN-Cheats-Collection";
  }

  const char *const *flattenRoots(size_t *count) const override {
    static const char *const kRoots[] = {"cheats"};
    if (count) {
      *count = 1;
    }
    return kRoots;
  }

};

} // namespace

const ICheatCatalog &hen_cheats_collection() {
  static const HenCheatsCollection kInstance;
  return kInstance;
}

} // namespace onion::cheats::sync
