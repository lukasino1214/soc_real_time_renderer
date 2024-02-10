#pragma once

#include <iostream>
#include <string_view>
#include <string>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <span>
#include <unordered_map>

#include <daxa/daxa.hpp>
#include <daxa/utils/task_graph.hpp>
#include <daxa/utils/pipeline_manager.hpp>
#include <daxa/utils/mem.hpp>

#define GLM_DEPTH_ZERO_TO_ONEW
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp>

using namespace daxa::types;

#include "graphics/shared.inl"