#include "serialize.h"
#include "../Symbols.h"

namespace ruby_typer {
namespace core {
namespace serialize {

const u4 VERSION = 2;

void GlobalStateSerializer::Pickler::putStr(const absl::string_view s) {
    putU4(s.size());
    constexpr int step = (sizeof(u4) / sizeof(u1));
    int end = (s.size() / step) * step;

    ENFORCE(step == 4);
    for (int i = 0; i < end; i += step) {
        put4U1((u1)s[i], (u1)s[i + 1], (u1)s[i + 2], (u1)s[i + 3]);
    }
    if (end != s.size()) {
        u4 acc = 0;
        for (int i = end; i < s.size(); i++) {
            acc = acc * 256u + (u1)s[i];
        }
        putU4(acc);
    }
}

char u4tochar(u4 u) {
    return *reinterpret_cast<char *>(&u);
}

std::string GlobalStateSerializer::UnPickler::getStr() {
    int sz = getU4();
    constexpr int step = (sizeof(u4) / sizeof(u1));
    int end = (sz / step) * step;
    std::string result(sz, '\0');
    for (int i = 0; i < end; i += step) {
        u4 el = getU4();
        result[i + 3] = u4tochar(el & 255u);
        result[i + 2] = u4tochar((el >> 8u) & 255u);
        result[i + 1] = u4tochar((el >> 16u) & 255u);
        result[i] = u4tochar((el >> 24u) & 255u);
    }
    if (end != result.size()) {
        u4 acc = getU4();
        for (int i = result.size() - 1; i >= end; i--) {
            result[i] = u4tochar(acc & 255u);
            acc = acc >> 8;
        }
    }
    return result;
}

void GlobalStateSerializer::Pickler::put4U1(u1 v1, u1 v2, u1 v3, u1 v4) {
    u4 uv1 = (u1)v1;
    u4 uv2 = (u1)v2;
    u4 uv3 = (u1)v3;
    u4 uv4 = (u1)v4;
    putU4((uv1 << 24u) | (uv2 << 16u) | (uv3 << 8u) | uv4);
}

void GlobalStateSerializer::UnPickler::get4U1(u1 &v1, u1 &v2, u1 &v3, u1 &v4) {
    u4 el = getU4();
    v4 = el & 255u;
    v3 = (el >> 8u) & 255u;
    v2 = (el >> 16u) & 255u;
    v1 = (el >> 24u) & 255u;
}

void GlobalStateSerializer::Pickler::putU4(const u4 u) {
    if (u != 0) {
        data.push_back(u);
    } else if (data.size() >= 2 && data[data.size() - 2] == 0 && data[data.size() - 1] != UINT_MAX) {
        data[data.size() - 1]++;
    } else {
        data.push_back(0);
        data.push_back(1);
    }
}

u4 GlobalStateSerializer::UnPickler::getU4() {
    if (zeroCounter != 0u) {
        zeroCounter--;
        return 0;
    }
    auto r = data[pos++];
    if (r == 0) {
        zeroCounter = data[pos++];
        zeroCounter--;
    }
    return r;
}

void GlobalStateSerializer::Pickler::putS8(const int64_t i) {
    putU4((u4)i);
    putU4((u4)(i >> 32));
}

int64_t GlobalStateSerializer::UnPickler::getS8() {
    u8 low = getU4();
    u8 high = getU4();
    u8 full = (high << 32) + low;
    return *reinterpret_cast<int64_t *>(&full);
}

void GlobalStateSerializer::pickle(Pickler &p, File &what) {
    p.putStr(what.path());
    p.putStr(what.source());
}

std::shared_ptr<File> GlobalStateSerializer::unpickleFile(UnPickler &p) {
    std::string path = p.getStr();
    std::string source = p.getStr();
    return std::make_shared<File>(move(path), move(source), File::Payload);
}

void GlobalStateSerializer::pickle(Pickler &p, Name &what) {
    p.putU4(what.kind);
    switch (what.kind) {
        case NameKind::UTF8:
            p.putStr(what.raw.utf8);
            break;
        case NameKind::UNIQUE:
            p.putU4(what.unique.uniqueNameKind);
            p.putU4(what.unique.original._id);
            p.putU4(what.unique.num);
            break;
        case NameKind::CONSTANT:
            p.putU4(what.cnst.original.id());
            break;
    }
}

Name GlobalStateSerializer::unpickleName(UnPickler &p, GlobalState &gs) {
    Name result;
    result.kind = (NameKind)p.getU4();
    switch (result.kind) {
        case NameKind::UTF8:
            result.kind = NameKind::UTF8;
            result.raw.utf8 = gs.enterString(p.getStr());
            break;
        case NameKind::UNIQUE:
            result.unique.uniqueNameKind = (UniqueNameKind)p.getU4();
            result.unique.original = NameRef(gs, p.getU4());
            result.unique.num = p.getU4();
            break;
        case NameKind::CONSTANT:
            result.cnst.original = NameRef(gs, p.getU4());
            break;
    }
    return result;
}

void GlobalStateSerializer::pickle(Pickler &p, Type *what) {
    if (what == nullptr) {
        p.putU4(0);
        return;
    }
    if (auto *c = cast_type<ClassType>(what)) {
        p.putU4(1);
        p.putU4(c->symbol._id);
    } else if (auto *o = cast_type<OrType>(what)) {
        p.putU4(2);
        pickle(p, o->left.get());
        pickle(p, o->right.get());
    } else if (auto *c = cast_type<LiteralType>(what)) {
        p.putU4(3);
        pickle(p, c->underlying.get());
        p.putS8(c->value);
    } else if (auto *a = cast_type<AndType>(what)) {
        p.putU4(4);
        pickle(p, a->left.get());
        pickle(p, a->right.get());
    } else if (auto *arr = cast_type<TupleType>(what)) {
        p.putU4(5);
        pickle(p, arr->underlying.get());
        p.putU4(arr->elems.size());
        for (auto &el : arr->elems) {
            pickle(p, el.get());
        }
    } else if (auto *hash = cast_type<ShapeType>(what)) {
        p.putU4(6);
        pickle(p, hash->underlying.get());
        p.putU4(hash->keys.size());
        ENFORCE(hash->keys.size() == hash->values.size());
        for (auto &el : hash->keys) {
            pickle(p, el.get());
        }
        for (auto &el : hash->values) {
            pickle(p, el.get());
        }
    } else if (auto *hash = cast_type<MagicType>(what)) {
        p.putU4(7);
    } else if (auto *alias = cast_type<AliasType>(what)) {
        p.putU4(8);
        p.putU4(alias->symbol._id);
    } else {
        Error::notImplemented();
    }
}

std::shared_ptr<Type> GlobalStateSerializer::unpickleType(UnPickler &p, GlobalState *gs) {
    switch (p.getU4()) {
        case 0: {
            std::shared_ptr<Type> empty;
            return empty;
        }
        case 1:
            return std::make_shared<ClassType>(SymbolRef(gs, p.getU4()));
        case 2:
            return OrType::make_shared(unpickleType(p, gs), unpickleType(p, gs));
        case 3: {
            std::shared_ptr<LiteralType> result = std::make_shared<LiteralType>(true);
            result->underlying = unpickleType(p, gs);
            result->value = p.getS8();
            return result;
        }
        case 4:
            return AndType::make_shared(unpickleType(p, gs), unpickleType(p, gs));
        case 5: {
            auto underlying = unpickleType(p, gs);
            int sz = p.getU4();
            std::vector<std::shared_ptr<Type>> elems;
            elems.reserve(sz);
            for (int i = 0; i < sz; i++) {
                elems.emplace_back(unpickleType(p, gs));
            }
            auto result = std::make_shared<TupleType>(elems);
            result->underlying = underlying;
            return result;
        }
        case 6: {
            auto underlying = unpickleType(p, gs);
            int sz = p.getU4();
            std::vector<std::shared_ptr<LiteralType>> keys;
            std::vector<std::shared_ptr<Type>> values;
            keys.reserve(sz);
            values.reserve(sz);
            for (int i = 0; i < sz; i++) {
                auto key = unpickleType(p, gs);
                if (auto *lit = cast_type<LiteralType>(key.get())) {
                    keys.emplace_back(lit);
                    key.reset();
                }
            }
            for (int i = 0; i < sz; i++) {
                values.emplace_back(unpickleType(p, gs));
            }
            auto result = std::make_shared<ShapeType>(keys, values);
            result->underlying = underlying;
            return result;
        }
        case 7:
            return std::make_shared<MagicType>();
        case 8:
            return std::make_shared<AliasType>(SymbolRef(gs, p.getU4()));
        default:
            Error::notImplemented();
    }
}

void GlobalStateSerializer::pickle(Pickler &p, Symbol &what) {
    p.putU4(what.owner._id);
    p.putU4(what.name._id);
    p.putU4(what.superClass._id);
    p.putU4(what.flags);
    p.putU4(what.uniqueCounter);
    p.putU4(what.argumentsOrMixins.size());
    for (SymbolRef s : what.argumentsOrMixins) {
        p.putU4(s._id);
    }
    p.putU4(what.members.size());
    for (auto member : what.members) {
        p.putU4(member.first.id());
        p.putU4(member.second._id);
    }

    pickle(p, what.resultType.get());
    p.putU4(what.definitionLoc.file.id());
    p.putU4(what.definitionLoc.begin_pos);
    p.putU4(what.definitionLoc.end_pos);
}

Symbol GlobalStateSerializer::unpickleSymbol(UnPickler &p, GlobalState *gs) {
    Symbol result;
    result.owner = SymbolRef(gs, p.getU4());
    result.name = NameRef(*gs, p.getU4());
    result.superClass = SymbolRef(gs, p.getU4());
    result.flags = p.getU4();
    result.uniqueCounter = p.getU4();
    int argumentsOrMixinsSize = p.getU4();
    result.argumentsOrMixins.reserve(argumentsOrMixinsSize);
    for (int i = 0; i < argumentsOrMixinsSize; i++) {
        result.argumentsOrMixins.emplace_back(SymbolRef(gs, p.getU4()));
    }
    int membersSize = p.getU4();
    result.members.reserve(membersSize);
    for (int i = 0; i < membersSize; i++) {
        auto name = NameRef(*gs, p.getU4());
        auto sym = SymbolRef(gs, p.getU4());
        result.members.push_back(std::make_pair(name, sym));
    }
    result.resultType = unpickleType(p, gs);

    result.definitionLoc.file = FileRef(*gs, p.getU4());
    result.definitionLoc.begin_pos = p.getU4();
    result.definitionLoc.end_pos = p.getU4();
    return result;
}

GlobalStateSerializer::Pickler GlobalStateSerializer::pickle(GlobalState &gs) {
    Pickler result;
    result.putU4(VERSION);
    result.putU4(gs.files.size());
    for (auto &f : gs.files) {
        pickle(result, *f);
    }

    result.putU4(gs.names.size());
    for (Name &n : gs.names) {
        pickle(result, n);
    }

    result.putU4(gs.symbols.size());
    for (Symbol &s : gs.symbols) {
        pickle(result, s);
    }

    result.putU4(gs.names_by_hash.size());
    for (auto &s : gs.names_by_hash) {
        result.putU4(s.first);
        result.putU4(s.second);
    }
    return result;
}

int nearestPowerOf2(int from) {
    int i = 1;
    while (i < from) {
        i = i * 2;
    }
    return i;
}

void GlobalStateSerializer::unpickleGS(UnPickler &p, GlobalState &result) {
    if (p.getU4() != VERSION) {
        Error::raise("Payload version mismatch");
    }

    std::vector<std::shared_ptr<File>> files(move(result.files));
    files.clear();
    std::vector<Name> names(move(result.names));
    names.clear();
    std::vector<Symbol> symbols(move(result.symbols));
    symbols.clear();
    std::vector<std::pair<unsigned int, unsigned int>> names_by_hash(move(result.names_by_hash));
    names_by_hash.clear();

    int filesSize = p.getU4();
    files.reserve(filesSize);
    for (int i = 0; i < filesSize; i++) {
        files.emplace_back(unpickleFile(p));
    }

    int namesSize = p.getU4();
    ENFORCE(namesSize > 0);
    names.reserve(nearestPowerOf2(namesSize));
    for (int i = 0; i < namesSize; i++) {
        names.emplace_back(unpickleName(p, result));
    }

    int symbolSize = p.getU4();
    ENFORCE(symbolSize > 0);
    symbols.reserve(symbolSize);
    for (int i = 0; i < symbolSize; i++) {
        symbols.emplace_back(unpickleSymbol(p, &result));
    }

    int namesByHashSize = p.getU4();
    names_by_hash.reserve(names.capacity() * 2);
    for (int i = 0; i < namesByHashSize; i++) {
        auto hash = p.getU4();
        auto value = p.getU4();
        names_by_hash.emplace_back(std::make_pair(hash, value));
    }
    result.files = move(files);
    result.names = move(names);
    result.symbols = move(symbols);
    result.names_by_hash = move(names_by_hash);
    result.sanityCheck();
}

std::vector<u4> GlobalStateSerializer::store(GlobalState &gs) {
    Pickler p = pickle(gs);
    return move(p.data);
}

void GlobalStateSerializer::load(GlobalState &gs, const u4 *const data) {
    ENFORCE(gs.files.empty() && gs.names.empty() && gs.symbols.empty(), "Can't load into a non-empty state");
    UnPickler p(data);
    unpickleGS(p, gs);
}
} // namespace serialize
} // namespace core
} // namespace ruby_typer
