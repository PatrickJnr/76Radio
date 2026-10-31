#include "radio76/icy_metadata.h"
#include "radio76/stream_player.h"
#include "radio76/stream_resolver.h"

#include <winrt/base.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <thread>

namespace {

int checks = 0;
int failures = 0;

void Check(bool condition, const char* expression, int line) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cout << "FAIL line " << line << ": " << expression << '\n';
    }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

struct Reply {
    std::string head;
    std::string body;
    bool closeAfterHead = false;
};

using Routes = std::map<std::string, std::function<Reply()>>;

class FixtureServer {
public:
    explicit FixtureServer(Routes routes) : routes_(std::move(routes)) {
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        int length = sizeof(address);
        getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length);
        port_ = ntohs(address.sin_port);
        listen(listener_, 8);
        worker_ = std::thread([this] { Serve(); });
    }

    ~FixtureServer() {
        running_ = false;
        closesocket(listener_);
        worker_.join();
    }

    std::string Url(const std::string& path) const {
        return "http://127.0.0.1:" + std::to_string(port_) + path;
    }

    int Port() const {
        return port_;
    }

private:
    void Serve() {
        while (running_) {
            const SOCKET client = accept(listener_, nullptr, nullptr);
            if (client == INVALID_SOCKET) {
                return;
            }
            std::thread([this, client] { Handle(client); }).detach();
        }
    }

    void Handle(SOCKET client) {
        std::string request;
        char buffer[1024];
        while (request.find("\r\n\r\n") == std::string::npos) {
            const int received = recv(client, buffer, sizeof(buffer), 0);
            if (received <= 0) {
                closesocket(client);
                return;
            }
            request.append(buffer, received);
        }
        const auto first = request.find(' ');
        const auto second = request.find(' ', first + 1);
        const std::string path = request.substr(first + 1, second - first - 1);
        const auto route = routes_.find(path);
        Reply reply;
        if (route == routes_.end()) {
            reply.head = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        } else {
            reply = route->second();
        }
        send(client, reply.head.data(), static_cast<int>(reply.head.size()), 0);
        if (!reply.closeAfterHead) {
            send(client, reply.body.data(), static_cast<int>(reply.body.size()), 0);
        }
        shutdown(client, SD_SEND);
        closesocket(client);
    }

    Routes routes_;
    SOCKET listener_ = INVALID_SOCKET;
    int port_ = 0;
    std::atomic<bool> running_{true};
    std::thread worker_;
};

Reply Ok(const std::string& type, const std::string& body) {
    return {
        "HTTP/1.1 200 OK\r\nContent-Type: " + type +
            "\r\nContent-Length: " + std::to_string(body.size()) +
            "\r\nConnection: close\r\n\r\n",
        body,
        false};
}

Reply Redirect(const std::string& location) {
    return {
        "HTTP/1.1 302 Found\r\nLocation: " + location +
            "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
        {},
        false};
}

bool WaitFor(const std::function<bool()>& condition, int seconds = 5) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return condition();
}

void TestResolver() {
    std::string base;
    Routes routes;
    routes["/direct"] = [] { return Ok("audio/mpeg", "ID3"); };
    routes["/redirect"] = [&] { return Redirect(base + "/direct"); };
    routes["/loop-a"] = [&] { return Redirect(base + "/loop-b"); };
    routes["/loop-b"] = [&] { return Redirect(base + "/loop-a"); };
    routes["/list.m3u"] = [&] { return Ok("audio/x-mpegurl", "#EXTM3U\n/direct\n"); };
    routes["/noext"] = [&] { return Ok("audio/x-scpls", "[playlist]\nFile1=" + base + "/direct\n"); };
    routes["/pointer"] = [&] { return Redirect(base + "/list.m3u"); };
    routes["/empty.m3u"] = [] { return Ok("audio/x-mpegurl", "#EXTM3U\n"); };
    routes["/self.m3u"] = [&] { return Ok("audio/x-mpegurl", base + "/self.m3u\n"); };
    routes["/nested.m3u"] = [&] { return Ok("audio/x-mpegurl", "/nested2.m3u\n"); };
    routes["/nested2.m3u"] = [&] { return Ok("audio/x-mpegurl", "/nested3.m3u\n"); };
    routes["/nested3.m3u"] = [&] { return Ok("audio/x-mpegurl", "/nested4.m3u\n"); };
    routes["/nested4.m3u"] = [&] { return Ok("audio/x-mpegurl", "/direct\n"); };
    routes["/huge.m3u"] = [] { return Ok("audio/x-mpegurl", std::string(400 * 1024, 'a')); };
    routes["/hls.m3u8"] = [] { return Ok("application/vnd.apple.mpegurl", "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXTINF:6,\nseg.ts\n"); };
    routes["/second-candidate.pls"] = [&] {
        return Ok("audio/x-scpls", "[playlist]\nFile1=" + base + "/missing.m3u\nFile2=" + base + "/direct\n");
    };
    FixtureServer server(routes);
    base = server.Url("");

    const auto direct = radio76::ResolveStreamUrl(base + "/direct");
    CHECK(direct.status == radio76::StreamResolveStatus::Resolved);
    CHECK(direct.playbackUrl == base + "/direct");
    CHECK(direct.metadataUrl == base + "/direct");

    const auto redirected = radio76::ResolveStreamUrl(base + "/redirect");
    CHECK(redirected.status == radio76::StreamResolveStatus::Resolved);
    CHECK(redirected.playbackUrl == base + "/direct");

    const auto m3u = radio76::ResolveStreamUrl(base + "/list.m3u");
    CHECK(m3u.status == radio76::StreamResolveStatus::Resolved);
    CHECK(m3u.playbackUrl == base + "/direct");

    const auto noExtension = radio76::ResolveStreamUrl(base + "/noext");
    CHECK(noExtension.status == radio76::StreamResolveStatus::Resolved);
    CHECK(noExtension.playbackUrl == base + "/direct");

    const auto redirectToPlaylist = radio76::ResolveStreamUrl(base + "/pointer");
    CHECK(redirectToPlaylist.status == radio76::StreamResolveStatus::Resolved);
    CHECK(redirectToPlaylist.playbackUrl == base + "/direct");

    const auto fallback = radio76::ResolveStreamUrl(base + "/second-candidate.pls");
    CHECK(fallback.status == radio76::StreamResolveStatus::Resolved);
    CHECK(fallback.playbackUrl == base + "/direct");

    CHECK(radio76::ResolveStreamUrl(base + "/empty.m3u").status == radio76::StreamResolveStatus::Failed);
    CHECK(radio76::ResolveStreamUrl(base + "/self.m3u").status == radio76::StreamResolveStatus::Failed);
    CHECK(radio76::ResolveStreamUrl(base + "/nested.m3u").status == radio76::StreamResolveStatus::Failed);
    CHECK(radio76::ResolveStreamUrl(base + "/huge.m3u").status == radio76::StreamResolveStatus::Failed);

    const auto hls = radio76::ResolveStreamUrl(base + "/hls.m3u8");
    CHECK(hls.status == radio76::StreamResolveStatus::Resolved);
    CHECK(hls.playbackUrl == base + "/hls.m3u8");
    CHECK(hls.metadataUrl.empty());

    const auto started = std::chrono::steady_clock::now();
    const auto loop = radio76::ResolveStreamUrl(base + "/loop-a");
    CHECK(loop.status != radio76::StreamResolveStatus::Failed || loop.playbackUrl.empty());
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(20));

    CHECK(radio76::ResolveStreamUrl("ftp://example.com/stream").status == radio76::StreamResolveStatus::Unsupported);
    CHECK(radio76::ResolveStreamUrl("file:///c:/radio.mp3").status == radio76::StreamResolveStatus::Unsupported);
    CHECK(radio76::ResolveStreamUrl("").status == radio76::StreamResolveStatus::Unsupported);
}

void TestResolverConnectionRefused() {
    int closedPort = 0;
    {
        FixtureServer server({});
        closedPort = server.Port();
    }
    const std::string url = "http://127.0.0.1:" + std::to_string(closedPort) + "/gone.m3u";
    CHECK(radio76::ResolveStreamUrl(url).status == radio76::StreamResolveStatus::Failed);
}

std::string IcyBody(const std::string& title, int interval) {
    std::string block = "StreamTitle='" + title + "';";
    while (block.size() % 16 != 0) {
        block.push_back('\0');
    }
    std::string body(interval, 'x');
    body.push_back(static_cast<char>(block.size() / 16));
    body += block;
    body += std::string(interval, 'x');
    body.push_back('\0');
    return body;
}

void TestIcyReader() {
    constexpr int interval = 64;
    Routes routes;
    routes["/icy"] = [] {
        const std::string body = IcyBody("Caf\xE9 - Song", interval);
        return Reply{
            "HTTP/1.0 200 OK\r\nICY-NAME: Fixture FM\r\nicy-metaint: " + std::to_string(interval) +
                "\r\nContent-Type: audio/mpeg\r\n\r\n",
            body,
            false};
    };
    routes["/plain"] = [] { return Ok("audio/mpeg", std::string(256, 'x')); };
    FixtureServer server(routes);

    radio76::IcyMetadataReader reader;
    reader.Start(server.Url("/icy"));
    CHECK(WaitFor([&] { return reader.Title() == "Caf\xC3\xA9 - Song"; }));
    CHECK(WaitFor([&] { return reader.Station() == "Fixture FM"; }));
    reader.Stop();
    CHECK(reader.Title().empty());
    CHECK(reader.Station().empty());

    reader.Start(server.Url("/plain"));
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    CHECK(reader.Title().empty());
    reader.Stop();

    reader.Start(server.Url("/icy"));
    reader.Stop();
    CHECK(reader.Title().empty());
}

void TestStreamPlayerLifecycle() {
    int closedPort = 0;
    {
        FixtureServer server({});
        closedPort = server.Port();
    }
    const std::string dead = "http://127.0.0.1:" + std::to_string(closedPort) + "/live";

    radio76::StreamPlayer player;
    player.SetVolume(0.0);
    player.SetVolume(2.0);
    player.SetVolume(0.4);
    CHECK(player.State() == radio76::PlaybackState::Stopped);
    CHECK(player.Start(dead));
    CHECK(player.Active());
    CHECK(player.State() != radio76::PlaybackState::Playing);
    CHECK(WaitFor([&] { return player.State() == radio76::PlaybackState::Reconnecting; }, 30));
    CHECK(player.Title().empty());
    player.Stop();
    CHECK(!player.Active());
    CHECK(player.State() == radio76::PlaybackState::Stopped);

    Routes unsupportedRoutes;
    unsupportedRoutes["/garbage"] = [] { return Ok("audio/mpeg", std::string(20000, 'q')); };
    FixtureServer unsupportedServer(unsupportedRoutes);
    CHECK(player.Start(unsupportedServer.Url("/garbage")));
    CHECK(WaitFor([&] { return player.State() == radio76::PlaybackState::Unsupported; }, 30));
    CHECK(player.Failed());
    player.Stop();

    for (int index = 0; index < 20; ++index) {
        CHECK(player.Start(dead + std::to_string(index)));
        CHECK(player.State() != radio76::PlaybackState::Playing);
    }
    player.Stop();
    CHECK(player.State() == radio76::PlaybackState::Stopped);

    CHECK(player.Start("ftp://example.com/stream"));
    CHECK(WaitFor([&] { return player.State() == radio76::PlaybackState::Unsupported; }));
    CHECK(player.Failed());
    player.Stop();
    CHECK(!player.Failed());
    CHECK(!player.Start(""));

    CHECK(player.Start(dead));
    const auto started = std::chrono::steady_clock::now();
    player.Shutdown();
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(3));
    CHECK(!player.Active());
}

using Clock = std::chrono::steady_clock;

long long ElapsedMs(Clock::time_point since) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - since).count();
}

struct Gate {
    std::atomic<bool> open{false};
    std::atomic<int> entered{0};

    Reply Hold(const std::string& body) {
        ++entered;
        for (int waited = 0; waited < 1500 && !open; ++waited) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return Ok("audio/x-mpegurl", body);
    }
};

void TestAsyncResolution() {
    auto gate = std::make_shared<Gate>();
    Routes routes;
    routes["/slow.m3u"] = [gate] { return gate->Hold("http://127.0.0.1:1/never\n"); };
    routes["/garbage"] = [] { return Ok("audio/mpeg", std::string(20000, 'q')); };
    auto attempts = std::make_shared<std::atomic<int>>(0);
    routes["/flaky.m3u"] = [attempts] {
        if (attempts->fetch_add(1) == 0) {
            return Reply{"HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", {}, false};
        }
        return Ok("audio/x-mpegurl", "/garbage\n");
    };
    FixtureServer server(routes);

    {
        radio76::StreamPlayer player;
        auto began = Clock::now();
        CHECK(player.Start(server.Url("/slow.m3u")));
        const long long startMs = ElapsedMs(began);
        std::cout << "start latency ms: " << startMs << '\n';
        CHECK(startMs < 250);
        CHECK(player.State() == radio76::PlaybackState::Connecting);
        CHECK(WaitFor([&] { return gate->entered.load() >= 1; }));
        CHECK(player.State() == radio76::PlaybackState::Connecting);

        began = Clock::now();
        player.Stop();
        const long long stopMs = ElapsedMs(began);
        std::cout << "stop latency ms: " << stopMs << '\n';
        CHECK(stopMs < 250);
        CHECK(player.State() == radio76::PlaybackState::Stopped);

        began = Clock::now();
        player.Shutdown();
        const long long cleanupMs = ElapsedMs(began);
        std::cout << "cleanup ms: " << cleanupMs << '\n';
        CHECK(cleanupMs < 1500);
    }
    gate->open = true;

    {
        radio76::StreamPlayer player;
        gate->open = false;
        gate->entered = 0;
        CHECK(player.Start(server.Url("/slow.m3u")));
        CHECK(WaitFor([&] { return gate->entered.load() >= 1; }));
        CHECK(player.Start("ftp://example.com/stream"));
        CHECK(WaitFor([&] { return player.State() == radio76::PlaybackState::Unsupported; }));
        gate->open = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        CHECK(player.State() == radio76::PlaybackState::Unsupported);
        player.Stop();
        CHECK(player.State() == radio76::PlaybackState::Stopped);
    }

    {
        radio76::StreamPlayer player;
        gate->open = false;
        gate->entered = 0;
        for (int index = 0; index < 12; ++index) {
            const auto began = Clock::now();
            CHECK(player.Start(server.Url("/slow.m3u?" + std::to_string(index))));
            CHECK(ElapsedMs(began) < 250);
        }
        const auto began = Clock::now();
        player.Shutdown();
        CHECK(ElapsedMs(began) < 1500);
        CHECK(!player.Active());
        gate->open = true;
    }

    {
        gate->open = false;
        gate->entered = 0;
        radio76::ResolveCancellation cancellation;
        std::thread canceller([&] {
            WaitFor([&] { return gate->entered.load() >= 1; });
            cancellation.Cancel();
        });
        const auto began = Clock::now();
        const auto result = radio76::ResolveStreamUrl(server.Url("/slow.m3u"), {}, &cancellation);
        canceller.join();
        CHECK(ElapsedMs(began) < 1500);
        CHECK(result.status != radio76::StreamResolveStatus::Resolved);
        gate->open = true;

        radio76::ResolveCancellation preCancelled;
        preCancelled.Cancel();
        CHECK(radio76::ResolveStreamUrl(server.Url("/garbage"), {}, &preCancelled).status !=
            radio76::StreamResolveStatus::Resolved);
    }

    {
        radio76::StreamPlayer player;
        CHECK(player.Start(server.Url("/flaky.m3u")));
        CHECK(WaitFor([&] { return player.State() == radio76::PlaybackState::Reconnecting; }));
        CHECK(WaitFor([&] { return player.State() == radio76::PlaybackState::Unsupported; }, 30));
        CHECK(attempts->load() >= 2);
        player.Stop();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
}

}

int main() {
    winrt::init_apartment();
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
    std::cout << "radio76 stream fixture tests\n";
    TestResolver();
    TestResolverConnectionRefused();
    TestIcyReader();
    TestStreamPlayerLifecycle();
    TestAsyncResolution();
    std::cout << checks << " checks, " << failures << " failures\n";
    WSACleanup();
    return failures == 0 ? 0 : 1;
}
