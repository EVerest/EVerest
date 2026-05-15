
#include <iso15118/ev/ev_controller.hpp>

int main() {

    iso15118::ev::EvController controller;

    controller.start_session();

    return 0;
}
