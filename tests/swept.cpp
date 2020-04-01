//
// Created by Carter Mbotho on 2020-03-27.
//

#include <suil/init.h>
#include <suil/cmdl.h>
#include <suil/http.h>
#include <suil/http/endpoint.h>

#include <wait.h>

#include "../src/gateway/gateway.scc.h"

using namespace suil;

typedef decltype(iod::D(
        prop(bin, String),
        prop(args(var(optional)), std::vector<String>)
)) Restart;

struct Launcher final {
    Launcher() = default;

    Launcher(const Launcher&) = delete;
    Launcher(Launcher&&) = delete;
    Launcher&operator=(const Launcher&) = delete;
    Launcher&operator=(Launcher&&) = delete;

    bool restart(const Restart& R) {
        mBinary = R.bin.dup();
        char *argv[R.args.size()+2];
        argv[0] = const_cast<char *>(mBinary());
        OBuffer ob;
        ob << mBinary << " ";
        for (int i = 0; i < R.args.size(); i++) {
            argv[i+1] = const_cast<char *>(R.args[i]());
            ob << R.args[i] << " ";
        }
        argv[R.args.size()+1] = nullptr;

        if (utils::fs::exists(".sweep")) {
            utils::fs::remove(".sweep");
        }

        if ((mPid = mfork()) == -1) {
            // nothing we can do
            throw Exception::create(
                    "Failed to fork process for binary '", mBinary, "':",
                    errno_s);
        }
        if (Ego.mPid == 0) {
            // child process, this is where we wanna launch our binary
            sdebug("Launching %s", ob.data());
            auto ret = ::execvp(Ego.mBinary(), argv);
            _exit(ret);
        }
        else {
            sdebug("Launched binary %s", ob.data());
            auto timeout = utils::after(5000);

            do {
                int  code{0};
                auto size{sizeof(code)};
                if (utils::fs::read(".sweep", &code, size)) {
                    return code == 0;
                }
                msleep(utils::after(500));
            } while (mnow() < timeout);

            return false;
        }
    }

    operator bool() {
        if (Ego.mPid == -1) {
            return false;
        }
        int status{0};
        int ret = waitpid(Ego.mPid, &status, WNOHANG);
        if (ret < 0) {
            if (errno != ECHILD)
                serror("Launcher {pid=%ld} waitpid failed: %s", Ego.mPid, errno_s);
            Ego.mPid = -1;
            return false;
        }

        if ((ret == Ego.mPid) && WIFEXITED(status)) {
            // process exited
            strace("Launcher {pid=%ld} exited status=%d", Ego.mPid, WEXITSTATUS(status));
            Ego.mPid = -1;
            return false;
        }
        return true;
    }

    bool stop() {
        if (Ego) {
            // kill the binary
            kill(Ego.mPid, SIGABRT);
            msleep(utils::after(1000));
            return Ego;
        }
        return true;
    }

    ~Launcher() {
        Ego.stop();
    }

private:
    String mBinary{""};
    pid_t  mPid{-1};
};

static void startMain(cmdl::Cmd& cmd)
{
    auto gtyurl = cmd.getvalue<String>("gtyurl", "http://localhost:10080");
    sdebug("Gateway URL is %s", gtyurl());

    http::Endpoint<> ep("", opt(port, 10084), opt(name, "0.0.0.0"));
    Launcher gateway;

    eproute(ep, "/restart")
    ("GET"_method)
    ([&gateway, &gtyurl](const http::Request& req, http::Response& resp) {
        try {
            auto args = req.toJson<Restart>();

            gateway.stop();
            auto timeout = utils::after(5000);
            while (gateway and (mnow() < timeout)) {
                msleep(utils::after(1000));
            }
            if (gateway) {
                // failed to stop gateway
                resp << "Failed to stop currently running instance of gateway";
                resp.end(http::INTERNAL_ERROR);
                return;
            }
            if (gateway.restart(args)) {
                resp << R"({"server": ")" << gtyurl << "\"}";
                resp.end(http::OK);
            }
            else {
                resp << "Restarting the server failed";
                resp.end(http::INTERNAL_ERROR);
            }
        }
        catch (...) {
            auto ex = Exception::fromCurrent();
            serror("/restart: %s", ex.what());
            resp << ex.what();
            resp.end(http::INTERNAL_ERROR);
        }
    });

    eproute(ep, "/stop")
    ("POST"_method)
    ([&gateway] {
        // stop the gateway
        gateway.stop();
        return http::Status::OK;
    });

    eproute(ep, "/running")
    ("GET"_method)
    ([&gateway] {
        // stop the gateway
        return gateway? http::OK : http::Status::RESET_CONTENT;
    });

    ep.start();
}

static  void cmdStart(cmdl::Parser& parser)
{
    cmdl::Cmd start("start", "starts gtytest server");
    start << cmdl::Arg{"gtyurl", "Base gateway URL with port (default: http://locahost:10080)",
                       'C', false};
    start(startMain);
    parser.add(std::move(start));
}

int main(int argc, char *argv[])
{
    suil::init(opt(printinfo, false));
    log::setup(opt(verbose, log::TRACE), opt(name, APP_NAME));
    cmdl::Parser parser(APP_NAME, APP_VERSION, "");
    FileLogger fileLogger("/tmp/semausu/gateway", "gtytest");
    log::setup(opt(sink, [&fileLogger](const char *msg, size_t size, log::Level l) {
        fileLogger.log(msg, size, l);
        // also log with default handler
        log::Handler()(msg, size, l);
    }));

    int code{EXIT_SUCCESS};
    try
    {
        cmdStart(parser);
        parser.parse(argc, argv);
        parser.handle();
    }
    catch(...)
    {
        fprintf(stderr, "error: %s\n", Exception::fromCurrent().what());
        code = EXIT_FAILURE;
    }

    fileLogger.close();
    return EXIT_SUCCESS;
}
