#pragma once

#include "lua.hpp"
#include "converter.hpp"
#include "value.hpp"
#include <cassert>
#include <algorithm>
#include <optional>

namespace clg {
    class ref {
    public:
        ref() = default;
        ref(const ref& other) noexcept: mLua(other.mLua), mPtr([&] {
            if (other.mLua) {
                other.push_value_to_stack();
                clg::checkThread();
                return incRef();
            }
            return -1;
        }()) {}
        ref(ref&& other) noexcept: mLua(other.mLua), mPtr(other.mPtr) {
            other.mLua = nullptr;
            other.mPtr = -1;
        }

        ref(std::nullptr_t): ref() {}

        template<typename T>
        static ref from_cpp(lua_State* lua, const T& t) {
            clg::push_to_lua(lua, t);
            return from_stack(lua);
        }

        ref& operator=(ref&& other) noexcept {
            mLua = other.mLua;
            mPtr = other.mPtr;
            other.mLua = nullptr;
            other.mPtr = -1;
            return *this;
        }
        ref& operator=(const ref& other) noexcept {
            if (mLua == other.mLua && mPtr == other.mPtr) return *this;
            mLua = other.mLua;
            mPtr = [&] {
                if (other.mLua) {
                    other.push_value_to_stack();
                    clg::checkThread();
                    return incRef();
                }
                return -1;
            }();
            return *this;
        }

        ~ref() {
            releaseIfNotNull();
        }


        ref& operator=(std::nullptr_t) noexcept {
            releaseIfNotNull();
            mPtr = -1;
            return *this;
        }

        static ref from_stack(lua_State* state) noexcept {
            assert(("from_stack with an empty stack?", lua_gettop(state) > 0));
            return { state };
        }

        clg::ref metatable() const noexcept {
            clg::stack_integrity_check check(mLua);
            push_value_to_stack();
            if (lua_getmetatable(mLua, -1)) {
                auto value = from_stack(mLua);
                lua_pop(mLua, 1);
                return value;
            }
            lua_pop(mLua, 1);
            return nullptr;
        }

        template<typename Value>
        void set_metatable(const Value& meta) noexcept {
            clg::stack_integrity_check check(mLua);
            push_value_to_stack();
            clg::push_to_lua(mLua, meta);
            lua_setmetatable(mLua, -2);
            lua_pop(mLua, 1);
        }

        void push_value_to_stack() const noexcept {
            assert(mLua != nullptr);
            lua_rawgeti(mLua, LUA_REGISTRYINDEX, mPtr);
        }

        std::string debug_str() const noexcept {
            if (isNull()) {
                return "\"nil\"";
            }
            clg::stack_integrity_check check(mLua);
            push_value_to_stack();
            auto s = clg::any_to_string(mLua, -1);
            lua_pop(mLua, 1);
            return s;
        }

        clg::value value() const noexcept {
            return as<clg::value>();
        }

        bool isNull() const noexcept {
            return mPtr == -1;
        }

        bool isFunction() const noexcept {
            clg::stack_integrity_check check(mLua);
            push_value_to_stack();
            bool value = lua_isfunction(mLua, -1);
            lua_pop(mLua, 1);
            return value;
        }

        explicit operator bool() const noexcept {
            return !isNull();
        }

        template<typename T>
        T as() const {
            assert(!isNull());
            stack_integrity_check check(mLua);
            push_value_to_stack();
            try {
                auto v = clg::get_from_lua<T>(mLua, -1);
                lua_pop(mLua, 1);
                return v;
            } catch (...) {
                lua_pop(mLua, 1);
                throw;
            }
            throw std::runtime_error("should not reach here");
        }

        template<typename T>
        std::optional<T> is() const {
            assert(!isNull());
            stack_integrity_check check(mLua);
            push_value_to_stack();
            try {
                auto v = clg::get_from_lua<T>(mLua, -1);
                lua_pop(mLua, 1);
                return v;
            } catch (...) {
                lua_pop(mLua, 1);
                return std::nullopt;
            }
            throw std::runtime_error("should not reach here");
        }

        lua_State* lua() const noexcept {
            return mLua;
        }

        [[nodiscard]]
        bool operator==(std::nullptr_t) const noexcept {
            return mPtr == -1;
        }

    private:
        lua_State* mLua = nullptr;
        int mPtr = -1;

        void releaseIfNotNull() {
            if (mPtr != -1) {
                clg::checkThread();
                luaL_unref(mLua, LUA_REGISTRYINDEX, mPtr);
            }
        }

        int incRef() {
            if (lua_isnil(mLua, -1)) {
                lua_pop(mLua, 1);
                return -1;
            }

            auto r = luaL_ref(mLua, LUA_REGISTRYINDEX);
            return r;
        }

        ref(lua_State* state) noexcept: mLua(state), mPtr(incRef()) {}
    };


    class table_view: public ref {
    public:
        using ref::ref;

        struct value_view {
        public:
            value_view(const table_view& table, const std::string_view& name) : table(table), name(name) {}

            [[nodiscard]]
            clg::ref ref() const noexcept {
                const auto L = table.lua();
                clg::stack_integrity_check c(L);
                table.push_value_to_stack();
                if (!lua_istable(L, -1)) {
                    lua_pop(L, 1);
                    return nullptr;
                }
                lua_getfield(L, -1, name.data());
                auto result = clg::ref::from_stack(L);
                lua_pop(L, 1);
                return result;
            }

            [[nodiscard]]
            explicit operator class ref() const noexcept {
                return ref();
            }

            template<typename T>
            const T& operator=(const T& t) const noexcept {
                const auto L = table.lua();
                clg::stack_integrity_check c(L);
                table.push_value_to_stack();
                lua_pushstring(L, name.data());
                clg::push_to_lua(L, t);
                lua_settable(L, -3);
                lua_pop(L, 1);
                return t;
            }

            template<typename... Args>
            void invokeNullsafe(Args&&... args);
        private:
            const table_view& table;
            std::string_view name;
        };

        table_view(ref r): ref(std::move(r)) {}


        template<typename K, typename V>
        void raw_set(const K& key, const V& value) const noexcept {
            const auto L = lua();
            clg::stack_integrity_check c(L);
            push_value_to_stack();
            clg::push_to_lua(L, key);
            clg::push_to_lua(L, value);
            lua_rawset(L, -3);
            lua_pop(L, 1);
        }

        value_view operator[](std::string_view v) const {
            assert(!isNull());
            return { *this, v };
        }
    };

    template<>
    struct converter<clg::ref> {
        static ref from_lua(lua_State* l, int n) {
            lua_pushvalue(l, n);
            return clg::ref::from_stack(l);
        }
        static int to_lua(lua_State* l, const clg::ref& ref) {
            if (ref.isNull()) {
                lua_pushnil(l);
                return 1;
            }
            ref.push_value_to_stack();
            return 1;
        }
    };

    template<>
    struct converter<clg::table_view>: converter<clg::ref> {};

}