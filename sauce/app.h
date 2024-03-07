#pragma once

#include <filesystem>

#include "core.h"
#include "render/render.h"
#include "update/entity.h"
#include "update/update.h"
#include "utils/config.h"

namespace VV {

Result get_resource_dir(std::filesystem::path &res_dir);

inline Entity_Coord get_cam_coord(const Entity &e);

constexpr u32 FPS = 60;
constexpr f32 FRAME_TIME_MILLIS = (1.0f / FPS) * 1000;

struct App {
  Update_State update_state;
  Render_State render_state;
  Config config;
};

Result handle_args(int argv, const char **argc, std::optional<u32> &world_seed);

Result poll_events(App &app);
Result init_app(App &app, int argv, const char **argc);
Result run_app(App &app);
void destroy_app(App &app);
}  // namespace VV
