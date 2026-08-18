#include "browser.h"

/* Form state lives in the LiteHTML document/container.  These small C
 * adapters keep the browser event loop independent of C++ implementation
 * details. */
void browser_form_clear_focus(void)
{
    browser_litehtml_form_clear_focus(browser_document);
}

int browser_form_input_active(void)
{
    return browser_litehtml_form_input_active(browser_document);
}

int browser_form_handle_key(struct leonos_gui_app_event *event)
{
    return browser_litehtml_form_handle_key(browser_document, event);
}
