#pragma once

#include <utility>

namespace ftl {

template <typename Fn>
class ScopeGuard
{
public:
        explicit ScopeGuard(Fn fn) : fn_(std::move(fn)), active_(true) {}

        ScopeGuard(ScopeGuard&& other) noexcept
                : fn_(std::move(other.fn_)), active_(other.active_)
        {
                other.active_ = false;
        }

        ScopeGuard& operator=(ScopeGuard&&) = delete;
        ScopeGuard(const ScopeGuard&) = delete;
        ScopeGuard& operator=(const ScopeGuard&) = delete;

        ~ScopeGuard()
        {
                if (active_)
                        fn_();
        }

        void dismiss() noexcept { active_ = false; }

private:
        Fn   fn_;
        bool active_;
};

template <typename Fn>
inline ScopeGuard<Fn> make_scope_guard(Fn&& fn)
{
        return ScopeGuard<Fn>(std::forward<Fn>(fn));
}

} // namespace ftl
