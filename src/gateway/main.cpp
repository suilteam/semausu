//
// Created by Carter Mbotho on 2020-03-25.
//
#include <suil/init.h>
#include <suil/cmdl.h>
#include "gateway.h"

using namespace suil;

static  void cmdStart(cmdl::Parser& parser) {
    cmdl::Cmd start("start", "starts semausu's gateway application server");
    start << cmdl::Arg{"config", "Path to an application configuration file",
                       'C', false};
    start << cmdl::Arg{"reset", "True if to reset databases, useful in testing environments",
                       'r', true, false};
    start(&nozama::Gateway::start);
    parser.add(std::move(start));
}

int main(int argc, char *argv[])
{
    suil::init(opt(printinfo, false));
    log::setup(opt(verbose, log::DEBUG), opt(name, APP_NAME));
    cmdl::Parser parser(APP_NAME, APP_VERSION, "");

    try
    {
        cmdStart(parser);
        parser.parse(argc, argv);
        parser.handle();
    }
    catch(...)
    {
        fprintf(stderr, "error: %s\n", Exception::fromCurrent().what());
#ifdef SWEPT
        if (!utils::fs::exists(".sweep")) {
            // write the exit code to file
            int code{EXIT_FAILURE};
            size_t  size{sizeof(code)};
            utils::fs::append(".sweep", &code, size);
        }
#endif
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
