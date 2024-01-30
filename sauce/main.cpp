#include "app.h"
#include <cstdlib>

using namespace YC;

int main(int argc, char *argv[]) {
  App *app = new App;
  LOG_INFO("Log initialized");

  if (init_app(*app) != Result::SUCCESS) {
    return EXIT_FAILURE;
  }
  run_app(*app);
  destroy_app(*app);

  delete app;

  return EXIT_SUCCESS;
}
