#include <spdlog/spdlog.h>

int main()
{
  spdlog::info("First breath!");
  spdlog::warn("Matching engine starting up...");
  spdlog::debug("This won't show unless you lower the level");

  // Lower the level to see debug/trace messages:
  spdlog::set_level(spdlog::level::debug);
  spdlog::debug("Now debug is visible");
}