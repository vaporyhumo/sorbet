#include "core/packages/PackageInfo.h"
#include "core/GlobalState.h"
#include "core/Loc.h"
#include "core/NameRef.h"
#include "core/Symbols.h"

using namespace std;

namespace sorbet::core::packages {
bool PackageInfo::exists() const {
    return mangledName().exists();
}

bool PackageInfo::operator==(const PackageInfo &rhs) const {
    return mangledName() == rhs.mangledName();
}

PackageInfo::~PackageInfo() {
    // see https://eli.thegreenplace.net/2010/11/13/pure-virtual-destructors-in-c
}

bool PackageInfo::isPackageModule(const core::GlobalState &gs, core::ClassOrModuleRef klass) {
    while (klass.exists() && klass != core::Symbols::root()) {
        if (klass == core::Symbols::PackageRegistry() || klass == core::Symbols::PackageTests()) {
            return true;
        }
        klass = klass.data(gs)->owner.asClassOrModuleRef();
    }
    return false;
}

bool PackageInfo::lexCmp(const std::vector<core::NameRef> &lhs, const std::vector<core::NameRef> &rhs) {
    return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                                        [](NameRef a, NameRef b) -> bool { return a.rawId() < b.rawId(); });
}

PackageNamespaceInfo PackageNamespaceInfo::load(const core::GlobalState &gs, core::NameRef package) {
    PackageNamespaceInfo res;
    res.package = package;

    auto &db = gs.packageDB();
    auto &packageInfo = db.getPackageInfo(package);
    auto &packageName = packageInfo.fullName();
    auto packageNameSize = packageName.size();

    for (auto pkg : db.packages()) {
        if (pkg == package) {
            continue;
        }

        auto &info = db.getPackageInfo(pkg);
        auto &fullName = info.fullName();

        // If this package's name is longer than the focused package, it might be a child package of the focused
        // package, otherwise it might be a parent package.
        if (packageNameSize < fullName.size()) {
            bool focusedIsPrefix = std::equal(packageName.begin(), packageName.end(), fullName.begin());
            if (focusedIsPrefix) {
                res.children.emplace_back(pkg);
            }
        } else {
            bool prefixOfFocused = std::equal(fullName.begin(), fullName.end(), packageName.begin());
            if (prefixOfFocused) {
                res.parents.emplace_back(pkg);
            }
        }
    }

    return res;
}

} // namespace sorbet::core::packages
