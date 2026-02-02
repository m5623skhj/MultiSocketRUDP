#include <utility>

namespace Util
{
    template <typename T>
    class ScopeExit
    {
    public:
        explicit ScopeExit(T&& function) : call(std::forward<T>(function)), active(true) {}
        ScopeExit(ScopeExit&& other) noexcept : call(std::move(other.call)), active(other.active)
        {
            other.active = false;
        }

        ~ScopeExit()
        {
            if (active)
            {
                call();
            }
        }

        ScopeExit(const ScopeExit&) = delete;
        ScopeExit& operator=(const ScopeExit&) = delete;

    private:
        T call;
        bool active;
    };

    template <typename F>
    ScopeExit<F> MakeScopeExit(F&& f)
    {
        return ScopeExit<F>(std::forward<F>(f));
    }
}
