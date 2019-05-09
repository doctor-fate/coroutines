#include <cinttypes>
#include <cstdio>
#include <experimental/coroutine>
#include <memory_resource>
#include <type_traits>
#include <utility>
#include <future>

namespace stdx = std::experimental;

template<typename T>
class TGenerator;

template<typename T>
class TGeneratorPromise {
public:
  using ValueType     = T;
  using ReferenceType = const ValueType &;
  using PointerType   = const ValueType *;

  template<typename ...Args>
  void *operator new(std::size_t Size, std::pmr::memory_resource *Resource, Args &&...) {
    const auto Buffer = Resource->allocate(Size + sizeof(Resource), alignof(decltype(Resource)));
    std::memcpy(Buffer, &Resource, sizeof(Resource));
    return ((std::byte *) Buffer) + sizeof(Resource);
  }

  void operator delete(void *Buffer, std::size_t Size) {
    std::pmr::memory_resource *Resource;
    void *Source = ((std::byte *) Buffer) - sizeof(Resource);
    std::memcpy(&Resource, Source, sizeof(Resource));
    Resource->deallocate(Source, Size + sizeof(Resource));
  }

  TGenerator<T> get_return_object() noexcept;

  constexpr stdx::suspend_always initial_suspend() const noexcept {
    return {};
  }

  constexpr stdx::suspend_always final_suspend() const noexcept {
    return {};
  }

  template<typename E>
  E &&await_transform(E &&Expression) = delete;

  template<typename U, typename = std::enable_if_t<std::is_assignable_v<T &, U>>>
  stdx::suspend_always yield_value(U &&Value) noexcept(noexcept(std::declval<T &>() = std::declval<U>())) {
    this->Value = std::forward<U>(Value);
    return {};
  }

  void unhandled_exception() noexcept { }

  void return_void() const noexcept { }

  decltype(auto) GetValue() const noexcept {
    return (Value);
  }
private:
  T Value;
};

template<typename T>
class TGeneratorIterator {
  using PromiseType = TGeneratorPromise<T>;
  using HandleType  = stdx::coroutine_handle<PromiseType>;
public:
  using iterator_category = std::input_iterator_tag;
  using difference_type   = std::ptrdiff_t;
  using value_type        = typename PromiseType::ValueType;
  using reference         = typename PromiseType::ReferenceType;
  using pointer           = typename PromiseType::PointerType;

  struct FSentinel { };

  constexpr TGeneratorIterator() noexcept = default;

  constexpr explicit TGeneratorIterator(HandleType Handle) noexcept : Handle(Handle) { }

  constexpr bool operator==(const TGeneratorIterator &Other) const noexcept {
    return Handle == Other.Handle;
  }

  constexpr bool operator==(FSentinel) const noexcept {
    return !Handle || Handle.done();
  }

  constexpr bool operator!=(const TGeneratorIterator &Other) const noexcept {
    return !(*this == Other);
  }

  constexpr bool operator!=(FSentinel Other) const noexcept {
    return !(*this == Other);
  }

  decltype(auto) operator++() noexcept {
    Handle.resume();
    return (*this);
  }

  void operator++(int) noexcept {
    (void) operator++();
  }

  decltype(auto) operator*() const noexcept {
    return Handle.promise().GetValue();
  }

  auto operator->() const noexcept {
    return std::addressof(operator*());
  }
private:
  HandleType Handle = nullptr;
};

template<typename T>
class [[nodiscard]] TGenerator{
  using PromiseType = TGeneratorPromise<T>;
  using HandleType  = stdx::coroutine_handle<PromiseType>;
public:
  using promise_type = PromiseType;

  explicit TGenerator(PromiseType &Promise) noexcept : Handle(HandleType::from_promise(Promise)) { }

  TGenerator(TGenerator &&Other) noexcept : Handle(std::exchange(Other.Handle, nullptr)) {

  }

  ~TGenerator() {
    if (Handle) {
      Handle.destroy();
    }
  }

  TGenerator &operator=(TGenerator &&Other) noexcept {
    std::swap(Handle, Other.Handle);
    return *this;
  }

  constexpr auto begin() const noexcept {
    TGeneratorIterator<T> Begin(Handle);
    if (Handle) {
      ++Begin;
    }
    return Begin;
  }

  constexpr auto end() const noexcept {
    return typename TGeneratorIterator<T>::FSentinel{};
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

template<typename T>
TGenerator<T> TGeneratorPromise<T>::get_return_object() noexcept {
  return TGenerator<T>(*this);
}

static TGenerator<std::uint64_t> Fibonacci(std::pmr::memory_resource *Resource, std::uint64_t K = 9) noexcept {
  std::uint64_t X = 0, Y = 1;
  for (int I = 0; I < K; ++I) {
    co_yield X = std::exchange(Y, X + Y);
  }
}

int main() {
  for (auto &&Value : Fibonacci(std::pmr::new_delete_resource())) {
    std::printf("%" PRIu64 "\n", Value);
  }

  return 0;
}