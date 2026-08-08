#include <stardustui/includes/window.hpp>
#include <stardustui/includes/components/button.hpp>
#include <stardustui/includes/components/lable.hpp>

namespace {
Lable *status_label = nullptr;

void show_click_result()
{
    if (status_label != nullptr) {
        status_label->set_text("Button clicked in LeonOS 4");
    }
}
}

extern "C" int main(int, char **, char **)
{
    Window window("StardustUI SDK Demo", 480, 260);
    Lable title("StardustUI on LeonOS 4", 24, 0x1C1B1EFF);
    Lable status("Ready", 16, 0x49454EFF);
    Button button("Click Me", 168, 44);

    title.set_pos(28, 28);
    status.set_pos(28, 194);
    button.set_pos(28, 88);
    button.callback(show_click_result);
    status_label = &status;

    window.addComponent(title);
    window.addComponent(button);
    window.addComponent(status);
    window.show();
    return 0;
}
