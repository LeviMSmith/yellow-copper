#include "render/render.h"

#include <iomanip>
#include <regex>
#include <sstream>

#include "SDL_surface.h"
#include "render/render_utils.h"
#include "render/texture.h"
#include "update/update.h"
#include "update/world.h"

namespace VV {

void playMusic(Render_State &render_state) {
  Mix_PlayMusic(render_state.current_music, -1);  // Play the music indefinitely
}

Result init_rendering(Render_State &render_state, Update_State &us,
                      Config &config) {
  // SDL init

  // Initting OGG and MP3 support
  int mix_flags = MIX_INIT_OGG | MIX_INIT_MP3;
  int initted = Mix_Init(mix_flags);
  if ((initted & mix_flags) != mix_flags) {
    LOG_ERROR(
        "Mix_Init: Failed to init required ogg and mp3 support! Error: {}",
        Mix_GetError());
    Mix_Quit();
    return Result::SDL_ERROR;
  }

  int sdl_init_flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO;

  SDL_ClearError();
  if (SDL_Init(sdl_init_flags) != 0) {
    LOG_ERROR("Failed to initialize sdl: {}", SDL_GetError());
    return Result::SDL_ERROR;
  }

  // Start the music playback thread
  render_state.music_loader_thread =
      std::thread(playMusic, std::ref(render_state));

  LOG_INFO("SDL initialized");

  // Initialize SDL_mixer
  if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
    LOG_ERROR("SDL_mixer could not initialize! SDL_mixer Error: {}",
              Mix_GetError());
    return Result::SDL_ERROR;
  }

  // Load the music files, set current music to forest music
  render_state.music_tracks[VV::Biome::FOREST] =
      Mix_LoadMUS("res/music/Forest(placeholder).mp3");
  render_state.music_tracks[VV::Biome::OCEAN] =
      Mix_LoadMUS("res/music/Ocean(placeholder).mp3");
  render_state.music_tracks[VV::Biome::ALASKA] =
      Mix_LoadMUS("res/music/Snow(placeholder).mp3");
  render_state.music_tracks[VV::Biome::NICARAGUA] =
      Mix_LoadMUS("res/music/Lava(placeholder).mp3");
  render_state.music_tracks[VV::Biome::DEEP_OCEAN] =
      Mix_LoadMUS("res/music/DeepOcean(placeholder).mp3");
  render_state.current_music = render_state.music_tracks[Biome::FOREST];
  if (!render_state.current_music) {
    LOG_ERROR("Failed to load music file: {}", Mix_GetError());
    return Result::SDL_ERROR;
  }

  // Play forest theme indefinitely
  Mix_PlayMusic(render_state.current_music, -1);

  LOG_DEBUG("Config window values: {}, {}", config.window_width,
            config.window_height);

  // Window
  SDL_ClearError();
  int window_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
  if (config.window_start_maximized) {
    window_flags = window_flags | SDL_WINDOW_MAXIMIZED;
    LOG_DEBUG("Starting window maximized");
  }

  render_state.window = SDL_CreateWindow(
      "Voyages & Verve", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      config.window_width, config.window_height, window_flags);

  SDL_ClearError();
  if (render_state.window == nullptr) {
    LOG_ERROR("Failed to create sdl window: {}", SDL_GetError());
    return Result::SDL_ERROR;
  }

  LOG_INFO("Window created");

  // Renderer
  SDL_ClearError();
  int renderer_flags = SDL_RENDERER_ACCELERATED;
  render_state.renderer =
      SDL_CreateRenderer(render_state.window, -1, renderer_flags);
  if (render_state.renderer == nullptr) {
    LOG_ERROR("Failed to create sdl renderer: {}", SDL_GetError());
    return Result::SDL_ERROR;
  }

  SDL_SetRenderDrawBlendMode(render_state.renderer, SDL_BLENDMODE_BLEND);

  // Do an initial resize to get all the base info from the screen loading into
  // the state
  Result resize_res = handle_window_resize(render_state, us);
  if (resize_res != Result::SUCCESS) {
    LOG_WARN("Failed to handle window resize! EC: {}",
             static_cast<s32>(resize_res));
  }

  // Create the world cell texture
  SDL_ClearError();
  render_state.cell_texture = SDL_CreateTexture(
      render_state.renderer, SDL_PIXELFORMAT_RGBA8888,
      SDL_TEXTUREACCESS_STREAMING, SCREEN_CHUNK_SIZE * CHUNK_CELL_WIDTH,
      SCREEN_CHUNK_SIZE * CHUNK_CELL_WIDTH);
  if (render_state.cell_texture == nullptr) {
    LOG_ERROR("Failed to create cell texture with SDL: {}", SDL_GetError());
    return Result::SDL_ERROR;
  }

  SDL_SetTextureBlendMode(render_state.cell_texture, SDL_BLENDMODE_BLEND);

  LOG_INFO("Created cell texture");

  // Create the rest of the textures from resources

  Result render_tex_res = init_render_textures(render_state, config);
  if (render_tex_res != Result::SUCCESS) {
    LOG_WARN(
        "Something went wrong while generating textures from resources. "
        "Going to try to continue.");
  } else {
    LOG_INFO("Created {} resource texture(s)", render_state.textures.size());
  }

  // Load fonts
  if (TTF_Init() == -1) {
    LOG_ERROR("Failed to initialize SDL_ttf: {}", TTF_GetError());
    return Result::SDL_ERROR;
  }

  std::filesystem::path main_font_path =
      config.res_dir / "fonts" / "dotty" / "dotty.ttf";
  render_state.main_font = TTF_OpenFont(main_font_path.string().c_str(), 48);
  if (render_state.main_font == nullptr) {
    LOG_ERROR("Failed to load main font: {}", TTF_GetError());
    return Result::SDL_ERROR;
  }

  return Result::SUCCESS;
}

Result render(Render_State &render_state, Update_State &update_state,
              const Config &config) {
  static u64 frame = 0;
  Entity &active_player = *get_active_player(update_state);

  if (update_state.events.find(Update_Event::PLAYER_MOVED_CHUNK) ==
      update_state.events.end()) {
    f64 player_x = active_player.coord.x + active_player.camx;
    f64 player_y = active_player.coord.y + active_player.camy;
    if (player_x < NICARAGUA_EAST_BORDER_CHUNK * CHUNK_CELL_WIDTH) {
      render_state.biome = Biome::NICARAGUA;
    } else if (player_x < FOREST_EAST_BORDER_CHUNK * CHUNK_CELL_WIDTH) {
      render_state.biome = Biome::FOREST;
    } else if (player_x < ALASKA_EAST_BORDER_CHUNK * CHUNK_CELL_WIDTH) {
      render_state.biome = Biome::ALASKA;
    } else {
      if (player_y < DEEP_SEA_LEVEL_CELL) {
        render_state.biome = Biome::DEEP_OCEAN;
      } else {
        render_state.biome = Biome::OCEAN;
      }
    }
  }

  // music stuff
  if (render_state.biome != render_state.current_biome) {
    render_state.current_biome =
        render_state.biome;  // if changed biome, switch current biome, and run
                             // code to change music
    Mix_Music *new_music =
        render_state.music_tracks
            [render_state.current_biome];  // set new music to the music for
                                           // whatever biome was just entered
    if (new_music !=
        render_state.current_music) {  // if the music tracks for the two biomes
                                       // are different continue w/ changing

      // NOTE (Levi): This call blocks execution, so should be called in another
      // thread.
      // Mix_FadeOutMusic(500);
      render_state.current_music = new_music;
      if (Mix_PlayMusic(render_state.current_music, -1) ==
          -1) {  // play new track indefinitely
        LOG_ERROR("Failed to play music: {}", Mix_GetError());
      }
    }
  }
  // end music stuff

  SDL_RenderClear(render_state.renderer);

  // Background
  switch (render_state.biome) {
    case Biome::NICARAGUA:
    case Biome::FOREST: {
      SDL_RenderCopy(render_state.renderer,
                     render_state.textures[(u8)Texture_Id::SKY].texture, NULL,
                     NULL);
      break;
    }
    case Biome::OCEAN:
    case Biome::ALASKA: {
      SDL_RenderCopy(render_state.renderer,
                     render_state.textures[(u8)Texture_Id::ALASKA_BG].texture,
                     NULL, NULL);
      break;
    }
    case Biome::DEEP_OCEAN: {
      break;
    }
  }

  // Mountains
  if (update_state.active_dimension == DimensionIndex::OVERWORLD &&
      render_state.biome != Biome::DEEP_OCEAN) {
    Res_Texture &mountain_tex =
        render_state.textures[(u8)Texture_Id::MOUNTAINS];
    SDL_Rect dest_rect = {
        static_cast<int>(active_player.coord.x * -0.1) -
            static_cast<int>(
                ((mountain_tex.width * render_state.screen_cell_size) -
                 render_state.window_width) *
                0.5),
        (render_state.window_height -
         (mountain_tex.height * render_state.screen_cell_size) + 128),
        mountain_tex.width * render_state.screen_cell_size,
        mountain_tex.height * render_state.screen_cell_size};

    SDL_RenderCopy(render_state.renderer,
                   render_state.textures[(u8)Texture_Id::MOUNTAINS].texture,
                   NULL, &dest_rect);
  } else if (render_state.biome == Biome::DEEP_OCEAN) {
    SDL_SetRenderDrawColor(render_state.renderer, 0x03, 0x01, 0x1e, 255);
    SDL_RenderFillRect(render_state.renderer, NULL);
  }

  // Cells and entities
  /*
  if (update_state.events.contains(Update_Event::PLAYER_MOVED_CHUNK) ||
      update_state.events.contains(Update_Event::CELL_CHANGE)) {
    gen_world_texture(render_state, update_state, config);
  }
  */
  gen_world_texture(render_state, update_state, config);

  render_entities(render_state, update_state, INT8_MIN, 20);
  render_cell_texture(render_state, update_state);

  // Alaska overlay
  if (render_state.biome == Biome::ALASKA) {
    SDL_SetRenderDrawColor(render_state.renderer, 255, 255, 255, 170);
    SDL_RenderFillRect(render_state.renderer, NULL);
  }

  render_entities(render_state, update_state, 21, INT8_MAX);

  render_hud(render_state, update_state);

  // Debug overlay
  static int w = 0, h = 0;
  if (frame % 20 == 0 && config.debug_overlay) {
    refresh_debug_overlay(render_state, update_state, w, h);
  }

  if (config.debug_overlay && render_state.debug_overlay_texture != nullptr) {
    SDL_Rect dest_rect = {0, 0, w, h};
    SDL_RenderCopy(render_state.renderer, render_state.debug_overlay_texture,
                   NULL, &dest_rect);
  }

  frame++;

  return Result::SUCCESS;
}

void destroy_rendering(Render_State &render_state) {
  if (render_state.main_font != nullptr) {
    TTF_CloseFont(render_state.main_font);
    LOG_INFO("Closed main font");
  }

  TTF_Quit();
  LOG_INFO("Quit SDL_ttf");

  if (render_state.cell_texture != nullptr) {
    SDL_DestroyTexture(render_state.cell_texture);
    LOG_INFO("Destroyed cell texture");
  }

  for (const std::pair<const u8, Res_Texture> &pair : render_state.textures) {
    SDL_DestroyTexture(pair.second.texture);
  }
  LOG_INFO("Destroyed {} resource textures", render_state.textures.size());

  if (render_state.debug_overlay_texture != nullptr) {
    SDL_DestroyTexture(render_state.debug_overlay_texture);
    LOG_INFO("Destroyed debug overlay texture");
  }

  if (render_state.window != nullptr) {
    SDL_DestroyWindow(render_state.window);
    LOG_INFO("Destroyed SDL window");
  }

  // Ensure the music playback thread is joined before cleaning up
  if (render_state.music_loader_thread.joinable()) {
    render_state.music_loader_thread.join();
  }

  // Free the music and close SDL_mixer
  if (render_state.current_music != nullptr) {
    Mix_FreeMusic(render_state.current_music);
  }
  Mix_CloseAudio();

  SDL_Quit();
  LOG_INFO("Quit SDL");
}

Result init_render_textures(Render_State &render_state, const Config &config) {
  render_state.debug_overlay_texture =
      nullptr;  // refresh function handles creation

  try {
    if (!std::filesystem::is_directory(config.tex_dir)) {
      LOG_ERROR("Can't initialize textures. {} is not a directory!",
                config.tex_dir.string());
      return Result::NONEXIST;
    }

    for (const auto &entry :
         std::filesystem::directory_iterator(config.tex_dir)) {
      if (entry.is_regular_file()) {
        std::regex pattern(
            "^([a-zA-Z0-9]+)-([0-9A-Fa-f]{2})\\.([a-zA-Z0-9]+)$");
        std::smatch matches;

        std::string filename = entry.path().filename().string();

        if (std::regex_match(filename, matches, pattern)) {
          if (matches.size() == 4) {  // matches[0] is the entire string, 1-3
                                      // are the capture groups
            std::string name = matches[1].str();
            std::string hexStr = matches[2].str();
            std::string extension = matches[3].str();

            unsigned int hexValue;
            std::stringstream ss;
            ss << std::hex << hexStr;
            ss >> hexValue;
            u8 id = static_cast<u8>(hexValue);

            if (id == 0) {
              LOG_ERROR("Texture {} id can't be 0!", entry.path().string());
              break;
            }

            if (extension == "bmp") {
              SDL_ClearError();
#ifdef __linux__
              SDL_Surface *bmp_surface = SDL_LoadBMP(entry.path().c_str());
#elif defined _WIN32
              SDL_Surface *bmp_surface =
                  SDL_LoadBMP(entry.path().string().c_str());
#elif defined __APPLE__
              SDL_Surface *bmp_surface = SDL_LoadBMP(entry.path().c_str());
#endif
              if (bmp_surface == nullptr) {
                LOG_ERROR(
                    "Failed to create surface for bitmap texture {}. SDL "
                    "error: {}",
                    entry.path().string(), SDL_GetError());
                break;
              }

              // Assume no conflicting texture ids, but check after with the
              // emplacement

              Res_Texture new_tex;

              SDL_ClearError();
              new_tex.texture = SDL_CreateTextureFromSurface(
                  render_state.renderer, bmp_surface);
              SDL_FreeSurface(
                  bmp_surface);  // Shouldn't overwrite any potential
                                 // errors from texture creation
              if (new_tex.texture == nullptr) {
                LOG_ERROR(
                    "Failed to create texture for bitmap texture {}. SDL "
                    "error: {}",
                    entry.path().string(), SDL_GetError());
                break;
              }

              SDL_ClearError();
              if (SDL_QueryTexture(new_tex.texture, NULL, NULL, &new_tex.width,
                                   &new_tex.height) != 0) {
                LOG_ERROR(
                    "Failed to get width and height for texture {}. Guessing. "
                    "SDL error: {}",
                    id, SDL_GetError());
                break;
              }

              auto emplace_res = render_state.textures.emplace(id, new_tex);
              if (!emplace_res.second) {
                LOG_ERROR(
                    "Couldn't create texture of id {} from texture {}. ID "
                    "already exists",
                    id, entry.path().string());
                SDL_DestroyTexture(new_tex.texture);
                break;
              }
            }
          }
        } else {
          LOG_WARN(
              "File {} in {} doesn't match the texture format. Skipping. "
              "Should be "
              "name-XX.ext",
              filename, config.tex_dir.string());
        }
      }
    }
  } catch (const std::filesystem::filesystem_error &e) {
    LOG_ERROR(
        "Something went wrong on the filesystem side while creating "
        "textures: {}",
        e.what());
    return Result::FILESYSTEM_ERROR;
  } catch (const std::exception &e) {
    LOG_ERROR("General standard library error while creating textures: {}",
              e.what());
    return Result::GENERAL_ERROR;
  }

  return Result::SUCCESS;
}

Result handle_window_resize(Render_State &render_state, Update_State &us) {
  SDL_ClearError();
  render_state.surface = SDL_GetWindowSurface(render_state.window);
  if (render_state.surface == nullptr) {
    // Log error but continue since the surface is not used
    LOG_ERROR("Failed to get SDL surface: {}", SDL_GetError());
    // return Result::SDL_ERROR; // Uncomment if you decide to handle this as an
    // error
  }

  // Get new window size
  SDL_GetWindowSize(render_state.window, &render_state.window_width,
                    &render_state.window_height);
  LOG_INFO("SDL window resized to {}, {}", render_state.window_width,
           render_state.window_height);
  us.window_width = render_state.window_width;
  us.window_height = render_state.window_height;

  // Update screen cell size based on new dimensions
  render_state.screen_cell_size =
      render_state.window_width / (SCREEN_CELL_SIZE_FULL - SCREEN_CELL_PADDING);
  us.screen_cell_size = render_state.screen_cell_size;

  return Result::SUCCESS;
}

Result gen_world_texture(Render_State &render_state, Update_State &update_state,
                         const Config &config) {
  // How to generate:
  // First need to find which chunks are centered around active_player cam.
  // Then essentially write the colors of each cell to the texture. Should
  // probably multithread.
  // TODO: multithread

  Entity &active_player = *get_active_player(update_state);

  // Remember, Entity::cam is relative to the entity's position
  f64 camx, camy;
  camx = active_player.camx + active_player.coord.x;
  camy = active_player.camy + active_player.coord.y;

  Chunk_Coord center = get_chunk_coord(camx, camy);
  if (center.x < 0) {
    center.x++;
  }
  if (center.y < 0) {
    center.y++;
  }
  Chunk_Coord ic;
  Chunk_Coord ic_max;
  u8 radius = SCREEN_CHUNK_SIZE / 2;

  // NOTE (Levi): There's no overflow checking on this right now.
  // Just don't go to the edge of the world I guess.
  ic.x = center.x - radius;
  ic.y = center.y - radius;

  ic_max.x = ic.x + SCREEN_CHUNK_SIZE;
  ic_max.y = ic.y + SCREEN_CHUNK_SIZE;

  // Update the top left chunk of the texture
  render_state.tl_tex_chunk = {ic.x, ic_max.y};

  u8 chunk_x = 0;
  u8 chunk_y = 0;

  Dimension &active_dimension = *get_active_dimension(update_state);

  // LOG_DEBUG("Generating world texture");
  // For each chunk in the texture...

  u32 *pixels;
  int pitch;
  SDL_ClearError();
  if (SDL_LockTexture(render_state.cell_texture, NULL, (void **)&pixels,
                      &pitch) != 0) {
    LOG_WARN("Failed to lock cell texture for updating: {}", SDL_GetError());
    return Result::SDL_ERROR;
  }

  assert(pitch == CHUNK_CELL_WIDTH * SCREEN_CHUNK_SIZE * sizeof(u32));
  constexpr u16 PITCH = CHUNK_CELL_WIDTH * SCREEN_CHUNK_SIZE;

  // For each chunk in the texture...
  u8 cr, cg, cb, ca;
  for (ic.y = center.y - radius; ic.y < ic_max.y; ic.y++) {
    for (ic.x = center.x - radius; ic.x < ic_max.x; ic.x++) {
      const auto &chunk_iter = active_dimension.chunks.find(ic);
      if (chunk_iter == active_dimension.chunks.end()) {
        continue;
      }
      const Chunk &chunk = chunk_iter->second;
#ifndef NDEBUG
      if (!(chunk.coord == ic)) {
        LOG_WARN(
            "Mapping of chunks failed! key: {}, {} chunk recieved: {}, "
            "{}",
            ic.x, ic.y, chunk.coord.x, chunk.coord.y);
      }
#endif

      // assert(chunk.coord == ic);
      // if (chunk.cells[0].type == Cell_Type::AIR) {
      //   LOG_DEBUG("Chunk %d, %d is a air chunk", ic.x, ic.y);
      // } else {
      //   LOG_DEBUG("Chunk %d, %d is a dirt chunk", ic.x, ic.y);
      // }

      for (u16 cell_y = 0; cell_y < CHUNK_CELL_WIDTH; cell_y++) {
        for (u16 cell_x = 0; cell_x < CHUNK_CELL_WIDTH; cell_x++) {
          // NOTE (Levi): Everything seems to be rotated 90 clockwise when
          // entered normally into this texture, so we swap x and y and subtract
          // x from it's max to mirror
          size_t buffer_index =
              (SCREEN_CHUNK_SIZE - 1 - chunk_y) * CHUNK_CELL_WIDTH * PITCH +
              ((CHUNK_CELL_WIDTH - 1 - cell_y) * PITCH) +
              (chunk_x * CHUNK_CELL_WIDTH) + cell_x;

          // Really need to get some unit tests going lol
#ifndef NDEBUG
          if (chunk_y == SCREEN_CHUNK_SIZE - 1 && chunk_x == 0 &&
              cell_y == CHUNK_CELL_WIDTH - 1 && cell_x == 0) {
            assert(buffer_index == 0);
          }

          if (chunk_y == 0 && chunk_x == 0 && cell_y == 0 && cell_x == 0) {
            assert(buffer_index == PITCH * PITCH - PITCH);
          }
#endif

          size_t cell_index = cell_x + cell_y * CHUNK_CELL_WIDTH;
          const Cell &cell = chunk.cells[cell_index];

          cr = cell.cr;
          cg = cell.cg;
          cb = cell.cb;
          ca = cell.ca;

          if (ic.x >= ALASKA_EAST_BORDER_CHUNK) {
            const s64 BONUS_DEEP_OCEAN_DEPTH = -30 * CHUNK_CELL_WIDTH;
            f32 t =
                1.0f -
                std::max(
                    std::min(((ic.y * CHUNK_CELL_WIDTH) + cell_y -
                              DEEP_SEA_LEVEL_CELL - BONUS_DEEP_OCEAN_DEPTH) /
                                 static_cast<f32>(SEA_LEVEL_CELL -
                                                  (DEEP_SEA_LEVEL_CELL +
                                                   BONUS_DEEP_OCEAN_DEPTH)),
                             1.0f),
                    0.0f);
            lerp(cr, cg, cb, ca, 0, 0, 0, 255, t);
          }

          if (config.debug_overlay) {
            if (cell_y == 0 && cell_x == 0) {
              if (chunk.all_cell != Cell_Type::WATER) {
                cr = 255;
                cg = 0;
                cb = 0;
                ca = 255;
              } else {
                cr = 0;
                cg = 0;
                cb = 255;
                ca = 255;
              }
            }
          }

          pixels[buffer_index] = (cr << 24) | (cg << 16) | (cb << 8) | ca;
          if (buffer_index >
              SCREEN_CHUNK_SIZE * SCREEN_CHUNK_SIZE * CHUNK_CELLS - 1) {
            LOG_ERROR(
                "Somehow surpassed the texture size while generating: {} "
                "cell texture chunk_x: {}, chunk_y: {}, cell_x: {}, cell_y: "
                "{}",
                buffer_index, chunk_x, chunk_y, cell_x, cell_y);
          }
          assert(buffer_index <=
                 SCREEN_CHUNK_SIZE * SCREEN_CHUNK_SIZE * CHUNK_CELLS - 1);
        }
      }

      chunk_x++;
    }
    chunk_x = 0;
    chunk_y++;
  }

  SDL_UnlockTexture(render_state.cell_texture);

  return Result::SUCCESS;
}

Result refresh_debug_overlay(Render_State &render_state,
                             const Update_State &update_state, int &w, int &h) {
  f64 x, y;
  x = update_state.entities[update_state.active_player].coord.x;
  y = update_state.entities[update_state.active_player].coord.y;
  u8 status = update_state.entities[update_state.active_player].status;

  // Create a stringstream
  std::stringstream debug_info_stream;

  // Use stream manipulators to format the floating-point numbers
  debug_info_stream << std::fixed << std::setprecision(1);  // For FPS
  debug_info_stream << "FPS: " << update_state.average_fps;

  // Reset stream format for other types
  debug_info_stream << std::setprecision(0);

  debug_info_stream
      << " | Dimension id: "
      << static_cast<unsigned>(update_state.active_dimension)
      << " Chunks loaded in dim "
      << update_state.dimensions.at(update_state.active_dimension).chunks.size()
      << " | Player pos: ";

  // Set precision for player position
  debug_info_stream << std::fixed << std::setprecision(2) << x << ", " << y;

  // Continue appending the rest of the information
  debug_info_stream << " Status: " << static_cast<u32>(status)
                    << " | World seed " << std::hex << std::setw(8)
                    << std::setfill('0') << update_state.world_seed;

  // Convert the stringstream to a string when you're ready to use it
  render_state.debug_info = debug_info_stream.str();

  if (render_state.debug_overlay_texture != nullptr) {
    SDL_DestroyTexture(render_state.debug_overlay_texture);
  }

  const SDL_Color do_fcolor = {255, 255, 255, 255};
  SDL_Surface *do_surface = TTF_RenderText_Blended(
      render_state.main_font, render_state.debug_info.c_str(), do_fcolor);
  if (do_surface == nullptr) {
    LOG_WARN("Failed to render debug info to a surface {}", TTF_GetError());
    return Result::SDL_ERROR;
  }

  w = do_surface->w;
  h = do_surface->h;

  SDL_ClearError();
  render_state.debug_overlay_texture =
      SDL_CreateTextureFromSurface(render_state.renderer, do_surface);
  if (render_state.debug_overlay_texture == nullptr) {
    LOG_WARN("Failed to create texture from debug overlay surface: {}",
             SDL_GetError());
    SDL_FreeSurface(do_surface);
    return Result::SDL_ERROR;
  }

  SDL_FreeSurface(do_surface);

  return Result::SUCCESS;
}

/*
Result render_trees(Render_State &rs, Update_State &us) {
  return Result::SUCCESS;
}
*/

Result render_cell_texture(Render_State &render_state,
                           Update_State &update_state) {
  Entity &active_player = *get_active_player(update_state);

  u16 screen_cell_size = render_state.screen_cell_size;

  Entity_Coord tl_chunk = get_world_pos_from_chunk(render_state.tl_tex_chunk);
  tl_chunk.y--;  // This is what makes it TOP left instead of bottom left

  // This is where the top left of the screen should be in world coordinates
  Entity_Coord good_tl_chunk;
  good_tl_chunk.x = active_player.camx + active_player.coord.x;
  good_tl_chunk.x -= (render_state.window_width / 2.0f) / screen_cell_size;

  good_tl_chunk.y = active_player.camy + active_player.coord.y;
  good_tl_chunk.y += (render_state.window_height / 2.0f) / screen_cell_size;

  s32 offset_x = (good_tl_chunk.x - tl_chunk.x) * screen_cell_size * -1;
  s32 offset_y = (tl_chunk.y - good_tl_chunk.y) * screen_cell_size * -1;

  /*
#ifndef NDEBUG
  if (offset_y > 0 || offset_x > 0) {
    LOG_WARN("Texture appears to be copied incorrectly. One of the offsets are
" "above 0: x:{}, y:{}", offset_x, offset_y);
  }
#endif
*/

  s32 width = screen_cell_size * SCREEN_CELL_SIZE_FULL;
  s32 height = screen_cell_size * SCREEN_CELL_SIZE_FULL;

  SDL_Rect destRect = {offset_x, offset_y, width, height};
  // SDL_Rect destRect = {10 * screen_cell_size, 10 * screen_cell_size,
  //                      SCREEN_CELL_SIZE_FULL * screen_cell_size,
  //                      SCREEN_CELL_SIZE_FULL * screen_cell_size};

  SDL_RenderCopy(render_state.renderer, render_state.cell_texture, NULL,
                 &destRect);

  return Result::SUCCESS;
}

Result render_entities(Render_State &render_state, Update_State &update_state,
                       Entity_Z z_min, Entity_Z z_thresh) {
  Dimension &active_dimension = *get_active_dimension(update_state);
  static std::set<Texture_Id> suppressed_id_warns;

  u16 screen_cell_size = render_state.screen_cell_size;
  Entity &active_player = *get_active_player(update_state);

  Entity_Coord tl;
  tl.x = active_player.camx + active_player.coord.x;
  tl.x -= (render_state.window_width / 2.0f) / screen_cell_size;

  tl.y = active_player.camy + active_player.coord.y;
  tl.y += (render_state.window_height / 2.0f) / screen_cell_size;

  for (const auto &[z, entity_index] : active_dimension.e_render) {
    if (z > z_thresh || z < z_min) {
      continue;
    }
    Entity &entity = update_state.entities[entity_index];

    auto sdk_texture =
        render_state.textures.find(static_cast<u8>(entity.texture));

    if (sdk_texture == render_state.textures.end()) {
      if (suppressed_id_warns.find(entity.texture) !=
          suppressed_id_warns.end()) {
        LOG_WARN("Entity wants texture {} which isn't loaded!",
                 (u8)entity.texture);
        suppressed_id_warns.insert(entity.texture);
      }
    } else {
      // Now it's time to render!

      Res_Texture &texture = sdk_texture->second;

      // LOG_DEBUG("entity coord: {} {}", entity.coord.x, entity.coord.y);
      // LOG_DEBUG("tl: {} {}", tl.x, tl.y);

      Entity_Coord world_offset;
      world_offset.x = entity.coord.x - tl.x;
      world_offset.y = tl.y - entity.coord.y;

      // LOG_DEBUG("World offset: {} {}", world_offset.x, world_offset.y);

      // TODO: No booleans! Maybe a separate animated entity collection in
      // dimension?
      //
      // TODO: Also need to account for a full sprite sheet. Have y indexes be
      // different states? i.e. walking anim, jumping, idle, etc.
      if (entity.status & (u8)Entity_Status::ANIMATED) {
        if (world_offset.x >= -entity.anim_width &&
            world_offset.x <= SCREEN_CELL_SIZE_FULL - SCREEN_CELL_PADDING +
                                  entity.anim_width &&
            world_offset.y >= -texture.height &&
            world_offset.y <= static_cast<s32>(render_state.window_height /
                                               render_state.screen_cell_size) +
                                  texture.height) {
          SDL_Rect src_rect = {
              entity.anim_width * entity.anim_current_frame, 0,
              entity.anim_width * (entity.anim_current_frame + 1),
              texture.height};

          SDL_Rect dest_rect = {
              (int)(world_offset.x * render_state.screen_cell_size),
              (int)(world_offset.y * render_state.screen_cell_size),
              entity.anim_width * screen_cell_size,
              texture.height * screen_cell_size};

          if (entity.flipped) {
            SDL_RenderCopyEx(render_state.renderer, texture.texture, &src_rect,
                             &dest_rect, 0, NULL, SDL_FLIP_HORIZONTAL);
          } else {
            SDL_RenderCopy(render_state.renderer, texture.texture, &src_rect,
                           &dest_rect);
          }
        }

        if (entity.anim_timer >
            entity.anim_delay + entity.anim_delay_current_spice) {
          entity.anim_current_frame = (entity.anim_current_frame + 1) %
                                      (texture.width / entity.anim_width);
          entity.anim_timer = 0;
          if (entity.anim_delay_variety > 0) {
            entity.anim_delay_current_spice =
                rand() % entity.anim_delay_variety;
          }
        }
        entity.anim_timer++;
      } else {
        // If visable
        if (world_offset.x >= -texture.width &&
            world_offset.x <=
                SCREEN_CELL_SIZE_FULL - SCREEN_CELL_PADDING + texture.width &&
            world_offset.y >= -texture.height &&
            world_offset.y <= static_cast<s32>(render_state.window_height /
                                               render_state.screen_cell_size) +
                                  texture.height) {
          SDL_Rect dest_rect = {
              (int)(world_offset.x * render_state.screen_cell_size),
              (int)(world_offset.y * render_state.screen_cell_size),
              texture.width * screen_cell_size,
              texture.height * screen_cell_size};

          if (entity.flipped) {
            SDL_RenderCopyEx(render_state.renderer, texture.texture, NULL,
                             &dest_rect, 0, NULL, SDL_FLIP_HORIZONTAL);
          } else {
            SDL_RenderCopy(render_state.renderer, texture.texture, NULL,
                           &dest_rect);
          }
        }
      }
    }
  }

  return Result::SUCCESS;
}

Result render_hud(Render_State &render_state, Update_State &update_state) {
  Entity &active_player = *get_active_player(update_state);

  // Draw active player health bar
  // First a black background
  SDL_SetRenderDrawColor(render_state.renderer, 0x33, 0x33, 0x33, 0xFF);

  const s64 HEALTH_MAX_WIDTH = 1000;
  int bar_width = std::min(active_player.max_health / 100, HEALTH_MAX_WIDTH);

  const int BAR_MARGIN = 30;
  const int BAR_HEIGHT = 20;
  SDL_Rect health_back_rect = {
      render_state.window_width - bar_width - BAR_MARGIN,  // x position
      BAR_MARGIN,                                          // y position
      bar_width,                                           // width
      BAR_HEIGHT                                           // height
  };
  SDL_RenderFillRect(render_state.renderer, &health_back_rect);

  // Now the red filling
  const int HEALTH_MARGIN = 2;
  int disp_health_width =
      std::max(std::min(active_player.health / 100,
                        HEALTH_MAX_WIDTH - (HEALTH_MARGIN * 2)),
               (s64)0);
  SDL_SetRenderDrawColor(render_state.renderer, 0xff, 0x33, 0x33, 0xFF);
  SDL_Rect health_bar = {render_state.window_width - bar_width - BAR_MARGIN -
                             HEALTH_MARGIN,           // x position
                         BAR_MARGIN + HEALTH_MARGIN,  // y position
                         disp_health_width, BAR_HEIGHT - (HEALTH_MARGIN * 2)};
  SDL_RenderFillRect(render_state.renderer, &health_bar);

  return Result::SUCCESS;
}
}  // namespace VV
