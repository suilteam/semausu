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

int main(int argc, const char *argv[])
{
    suil::init(opt(printinfo, false));
    try {
        http::Endpoint<> ep("", opt(port, 10084), opt(name, "0.0.0.0"));
        Launcher gateway;

        eproute(ep, "/restart")
        ("GET"_method)
        ([&gateway](const http::Request& req, http::Response& resp) {
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
                    resp << R"({"server": "http://localhost:10080"})";
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
    catch (...) {
        fprintf(stderr, "error: %s", suil::Exception::fromCurrent().what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
