#include <cinttypes>
#include <cstdio>
#include <future>
#include <type_traits>
#include <utility>

#include <experimental/coroutine>

namespace stdx = std::experimental;

template <typename T>
class TGenerator;

template <typename T>
class TGeneratorPromise {
public:
    using ValueType = T;
    using ReferenceType = const ValueType&;
    using PointerType = const ValueType*;

    TGenerator<T> get_return_object() noexcept;

    constexpr stdx::suspend_always initial_suspend() const noexcept {
        return {};
    }

    constexpr stdx::suspend_always final_suspend() const noexcept {
        return {};
    }

    template <typename E>
    E&& await_transform(E&& Expression) = delete;

    template <typename U>
    stdx::suspend_always yield_value(U&& Init) noexcept(noexcept(std::declval<T&>() = std::declval<U>())) {
        Value = std::forward<U>(Init);
        return {};
    }

    void unhandled_exception() noexcept {}

    void return_void() const noexcept {}

    decltype(auto) GetValue() const noexcept {
        return (Value);
    }

private:
    T Value;
};

template <typename T>
class TGeneratorIterator {
    using PromiseType = TGeneratorPromise<T>;
    using HandleType = stdx::coroutine_handle<PromiseType>;

public:
    using iterator_category = std::input_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = typename PromiseType::ValueType;
    using reference = typename PromiseType::ReferenceType;
    using pointer = typename PromiseType::PointerType;

    constexpr bool operator==(const TGeneratorIterator& Other) const noexcept {
        return Handle == Other.Handle;
    }

    constexpr bool operator!=(const TGeneratorIterator& Other) const noexcept {
        return !(*this == Other);
    }

    decltype(auto) operator++() {
        if (Handle(); Handle.done()) {
            Handle = nullptr;
        }

        return (*this);
    }

    void operator++(int) noexcept {
        (void) operator++();
    }

    decltype(auto) operator*() const noexcept {
        return Handle.promise().GetValue();
    }

    auto operator-> () const noexcept {
        return std::addressof(operator*());
    }

    HandleType Handle = nullptr;
};

template <typename T>
class [[nodiscard]] TGenerator {
    using PromiseType = TGeneratorPromise<T>;
    using HandleType = stdx::coroutine_handle<PromiseType>;

public:
    using promise_type = PromiseType;

    explicit TGenerator(PromiseType & Promise) noexcept : Handle(HandleType::from_promise(Promise)) {}

    TGenerator(TGenerator && Other) noexcept : Handle(std::exchange(Other.Handle, nullptr)) {}

    ~TGenerator() {
        if (Handle) {
            Handle.destroy();
        }
    }

    TGenerator& operator=(TGenerator&& Other) noexcept {
        std::swap(Handle, Other.Handle);
        return *this;
    }

    constexpr auto begin() const {
        return ++TGeneratorIterator<T>{Handle};
    }

    constexpr auto end() const noexcept {
        return TGeneratorIterator<T>{};
    }

    constexpr auto cbegin() const noexcept {
        return begin();
    }

    constexpr auto cend() const noexcept {
        return end();
    }

private:
    friend TGeneratorPromise<T>;

    HandleType Handle;
};

template <typename T>
TGenerator<T> TGeneratorPromise<T>::get_return_object() noexcept {
    return TGenerator<T>(*this);
}

static TGenerator<std::uint64_t> Fibonacci(std::uint64_t K = 9) noexcept {
    std::uint64_t Prev = 0, Current = 1;
    for (int I = 0; I < K; ++I) {
        Prev = std::exchange(Current, Prev + Current);
        co_yield Prev;
    }
}

int main() {
    for (auto Value: Fibonacci()) {
        std::printf("%" PRIu64 "\n", Value);
    }

    return 0;
}