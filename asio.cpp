#include <vector>

#include <boost/asio.hpp>
#include <experimental/coroutine>

namespace stdx = std::experimental;
namespace net = boost::asio;

template <typename MutableBuffer>
auto AsyncReadSome(net::ip::tcp::socket& Socket, MutableBuffer&& Buffer) {
    struct FAwaiter {
        FAwaiter(net::ip::tcp::socket& Socket, MutableBuffer&& Buffer) :
            Socket(Socket), Buffer(std::forward<MutableBuffer>(Buffer)) {}

        constexpr bool await_ready() const noexcept {
            return false;
        }

        bool await_suspend(stdx::coroutine_handle<> Handle) {
            Socket.async_read_some(Buffer, [this, Handle](auto&& Code, auto&& Received) mutable {
                this->Received = std::forward<decltype(Received)>(Received);
                this->Code = std::forward<decltype(Code)>(Code);
                Handle.resume();
            });
            return true;
        }

        auto await_resume() noexcept {
            return std::make_pair(std::move(Received), std::move(Code));
        }

        net::ip::tcp::socket& Socket;
        MutableBuffer Buffer;
        std::size_t Received = 0;
        boost::system::error_code Code;
    };

    return FAwaiter(Socket, std::forward<MutableBuffer>(Buffer));
}

template <typename ConstBuffer>
auto AsyncWrite(net::ip::tcp::socket& Socket, ConstBuffer&& Buffer) {
    struct FAwaiter {
        FAwaiter(net::ip::tcp::socket& Socket, ConstBuffer&& Buffer) :
            Socket(Socket), Buffer(std::forward<ConstBuffer>(Buffer)) {}

        constexpr bool await_ready() const noexcept {
            return false;
        }

        bool await_suspend(stdx::coroutine_handle<> Handle) {
            net::async_write(Socket, Buffer, [this, Handle](auto&& Code, auto&& Transferred) mutable {
                this->Transferred = std::forward<decltype(Transferred)>(Transferred);
                this->Code = std::forward<decltype(Code)>(Code);
                Handle.resume();
            });
            return true;
        }

        auto await_resume() noexcept {
            return std::make_pair(std::move(Transferred), std::move(Code));
        }

        net::ip::tcp::socket& Socket;
        ConstBuffer Buffer;
        std::size_t Transferred = 0;
        boost::system::error_code Code;
    };

    return FAwaiter(Socket, std::forward<ConstBuffer>(Buffer));
}

auto AsyncAccept(net::ip::tcp::acceptor& Acceptor) {
    struct FAwaiter {
        explicit FAwaiter(net::ip::tcp::acceptor& Acceptor) : Acceptor(Acceptor), Socket(Acceptor.get_executor()) {}

        constexpr bool await_ready() const noexcept {
            return false;
        }

        bool await_suspend(stdx::coroutine_handle<> Handle) {
            Acceptor.async_accept(Socket, [this, Handle](auto&& Code) mutable {
                this->Code = std::forward<decltype(Code)>(Code);
                Handle.resume();
            });
            return true;
        }

        auto await_resume() noexcept {
            return std::make_pair(std::move(Socket), std::move(Code));
        }

        net::ip::tcp::acceptor& Acceptor;
        net::ip::tcp::socket Socket;
        boost::system::error_code Code;
    };

    return FAwaiter(Acceptor);
}

class FAsyncTask;

struct FAsyncTaskPromise {
    FAsyncTask get_return_object() noexcept;

    constexpr stdx::suspend_always initial_suspend() const noexcept {
        return {};
    }

    constexpr stdx::suspend_never final_suspend() const noexcept {
        return {};
    }

    void unhandled_exception() noexcept {}

    void return_void() const noexcept {}
};

class FAsyncTask {
    using PromiseType = FAsyncTaskPromise;
    using HandleType = stdx::coroutine_handle<PromiseType>;

public:
    using promise_type = PromiseType;

    explicit FAsyncTask(PromiseType& Promise) noexcept : Handle(HandleType::from_promise(Promise)) {}

    FAsyncTask(FAsyncTask&& Other) noexcept : Handle(std::exchange(Other.Handle, nullptr)) {}

    ~FAsyncTask() {
        if (Handle) {
            Handle.destroy();
        }
    }

    FAsyncTask& operator=(FAsyncTask&& Other) noexcept {
        std::swap(Handle, Other.Handle);
        return *this;
    }

    void operator()(net::io_context& Context) && {
        net::post(Context, [Handle = std::exchange(Handle, nullptr)]() mutable { Handle.resume(); });
    }

private:
    friend FAsyncTaskPromise;

    HandleType Handle;
};

FAsyncTask FAsyncTaskPromise::get_return_object() noexcept {
    return FAsyncTask(*this);
}

FAsyncTask StartSession(net::io_context& Context, net::ip::tcp::socket Socket) {
    std::vector<std::byte> Buffer(1024);

    while (true) {
        const auto [Received, Code] = co_await AsyncReadSome(Socket, net::buffer(data(Buffer), size(Buffer)));
        if (Code) {
            break;
        }
        if (const auto [Transferred, Code] = co_await AsyncWrite(Socket, net::buffer(data(Buffer), Received)); Code) {
            break;
        }
    }
}

FAsyncTask StartListening(net::io_context& Context, const net::ip::tcp::endpoint& Endpoint) {
    net::ip::tcp::acceptor Acceptor(Context, Endpoint);
    Acceptor.listen();
    while (true) {
        if (auto [Socket, Code] = co_await AsyncAccept(Acceptor); !Code) {
            StartSession(Context, std::move(Socket))(Context);
        }
    }
}

int main() {
    net::io_context Context(1);

    net::signal_set Signals(Context, SIGINT, SIGTERM);
    Signals.async_wait([&Context](auto&&, auto&&) { Context.stop(); });

    const net::ip::tcp::endpoint Endpoint(net::ip::make_address("127.0.0.1"), 3386);

    StartListening(Context, Endpoint)(Context);

    Context.run();

    return 0;
}