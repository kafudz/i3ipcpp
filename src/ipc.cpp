#include "i3ipc++/ipc.hpp"

#include <json/json.h>
#include <sys/wait.h>

#include <auss.hpp>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>

#include "i3ipc++/ipc-util.hpp"
#include "i3ipc++/log.hpp"

namespace i3ipc {

// For log.hpp
std::vector<std::ostream*> g_logging_outs = {
    &std::cout,
};
std::vector<std::ostream*> g_logging_err_outs = {
    &std::cerr,
};

#define IPC_JSON_READ(ROOT)                                                                                        \
  {                                                                                                                \
    Json::CharReaderBuilder b;                                                                                     \
    const std::unique_ptr<Json::CharReader> reader(b.newCharReader());                                             \
    JSONCPP_STRING error;                                                                                          \
    if (!reader->parse(buf->payload, buf->payload + buf->header->size, &ROOT, &error)) {                           \
      throw invalid_reply_payload_error(auss_t() << "Failed to parse reply on \"" i3IPC_TYPE_STR "\": " << error); \
    }                                                                                                              \
  }

#define IPC_JSON_ASSERT_TYPE(OBJ, OBJ_DESCR, TYPE_CHECK, TYPE_NAME)                                                \
  {                                                                                                                \
    if (!(OBJ).TYPE_CHECK()) {                                                                                     \
      throw invalid_reply_payload_error(                                                                           \
          auss_t() << "Failed to parse reply on \"" i3IPC_TYPE_STR "\": " OBJ_DESCR " expected to be " TYPE_NAME); \
    }                                                                                                              \
  }
#define IPC_JSON_ASSERT_TYPE_OBJECT(OBJ, OBJ_DESCR) IPC_JSON_ASSERT_TYPE(OBJ, OBJ_DESCR, isObject, "an object")
#define IPC_JSON_ASSERT_TYPE_ARRAY(OBJ, OBJ_DESCR) IPC_JSON_ASSERT_TYPE(OBJ, OBJ_DESCR, isArray, "an array")
#define IPC_JSON_ASSERT_TYPE_BOOL(OBJ, OBJ_DESCR) IPC_JSON_ASSERT_TYPE(OBJ, OBJ_DESCR, isBool, "a bool")
#define IPC_JSON_ASSERT_TYPE_INT(OBJ, OBJ_DESCR) IPC_JSON_ASSERT_TYPE(OBJ, OBJ_DESCR, isInt, "an integer")
#define IPC_JSON_ASSERT_TYPE_STRING(OBJ, OBJ_DESCR) IPC_JSON_ASSERT_TYPE(OBJ, OBJ_DESCR, isString, "a string")

static rect_t parse_rect_from_json(const Json::Value& value) {
  rect_t r{};
  r.x = value["x"].asInt64();
  r.y = value["y"].asInt64();
  r.width = value["width"].asUInt();
  r.height = value["height"].asUInt();
  return r;
}

static window_properties_t parse_window_props_from_json(const Json::Value&  value) {
	if (value.isNull()) {
		window_properties_t result;
		result.transient_for = 0ull;
		return result;
	}

	window_properties_t result {
		value["class"].asString(),
		value["instance"].asString(),
		value["window_role"].asString(),
		value["title"].asString(),
		0ull
	};

	const Json::Value transient_for = value["transient_for"];
	if (!transient_for.isNull()) {
		result.transient_for = transient_for.asUInt64();
	}

	return result;
}


static std::shared_ptr<container_t> parse_container_from_json(const Json::Value& o) {
#define i3IPC_TYPE_STR "PARSE CONTAINER FROM JSON"
  if (o.isNull())
    return std::shared_ptr<container_t>();
  IPC_JSON_ASSERT_TYPE_OBJECT(o, "o")
  std::shared_ptr<container_t> container(new container_t());

  container->id = o["id"].asUInt64();
  container->xwindow_id = o["window"].asUInt64();
  container->name = o["name"].asString();
  container->type = o["type"].asString();
  container->current_border_width = o["current_border_width"].asInt();
  container->percent = o["percent"].asFloat();
  container->rect = parse_rect_from_json(o["rect"]);
  container->window_rect = parse_rect_from_json(o["window_rect"]);
  container->deco_rect = parse_rect_from_json(o["deco_rect"]);
  container->geometry = parse_rect_from_json(o["geometry"]);
  container->urgent = o["urgent"].asBool();
  container->focused = o["focused"].asBool();

  container->border = BorderStyle::UNKNOWN;
  std::string border = o["border"].asString();
  if (border == "normal") {
    container->border = BorderStyle::NORMAL;
  } else if (border == "none") {
    container->border = BorderStyle::NONE;
  } else if (border == "pixel") {
    container->border = BorderStyle::PIXEL;
  } else if (border == "1pixel") {
    container->border = BorderStyle::ONE_PIXEL;
  } else {
    container->border_raw = border;
    I3IPC_WARN("Got a unknown \"border\" property: \""
               << border
               << "\". Perhaps its neccessary to update i3ipc++. If you are using latest, note maintainer about this")
  }

  container->layout = ContainerLayout::UNKNOWN;
  std::string layout = o["layout"].asString();

  if (layout == "splith") {
    container->layout = ContainerLayout::SPLIT_H;
  } else if (layout == "splitv") {
    container->layout = ContainerLayout::SPLIT_V;
  } else if (layout == "stacked") {
    container->layout = ContainerLayout::STACKED;
  } else if (layout == "tabbed") {
    container->layout = ContainerLayout::TABBED;
  } else if (layout == "dockarea") {
    container->layout = ContainerLayout::DOCKAREA;
  } else if (layout == "output") {
    container->layout = ContainerLayout::OUTPUT;
  } else {
    container->layout_raw = layout;
    I3IPC_WARN("Got a unknown \"layout\" property: \""
               << layout
               << "\". Perhaps its neccessary to update i3ipc++. If you are using latest, note maintainer about this")
  }

  const Json::Value& nodes = o["nodes"];
  if (!nodes.isNull()) {
    IPC_JSON_ASSERT_TYPE_ARRAY(nodes, "nodes")
    for (Json::ArrayIndex i = 0; i < nodes.size(); i++) {
      container->nodes.push_back(parse_container_from_json(nodes[i]));
    }
  }

  const Json::Value& floating_nodes = o["floating_nodes"];
  if (!floating_nodes.isNull()) {
    IPC_JSON_ASSERT_TYPE_ARRAY(floating_nodes, "floating_nodes")
    for (Json::ArrayIndex i = 0; i < floating_nodes.size(); i++) {
      container->floating_nodes.push_back(parse_container_from_json(floating_nodes[i]));
    }
  }

  container->window_properties = parse_window_props_from_json(o["window_properties"]);

  return container;
#undef i3IPC_TYPE_STR
}

static std::shared_ptr<workspace_t> parse_workspace_from_json(const Json::Value& value) {
  if (value.isNull())
    return std::shared_ptr<workspace_t>();

  std::shared_ptr<workspace_t> p(new workspace_t());
  p->num = value["num"].asInt();
  p->name = value["name"].asString();
  p->visible = value["visible"].asBool();
  p->focused = value["focused"].asBool();
  p->urgent = value["urgent"].asBool();
  p->rect = parse_rect_from_json(value["rect"]);
  p->output = value["output"].asString();
  return p;
}

static std::shared_ptr<output_t> parse_output_from_json(const Json::Value& value) {
  if (value.isNull())
    return std::shared_ptr<output_t>();
  const Json::Value& current_workspace = value["current_workspace"];

  std::shared_ptr<output_t> p(new output_t());
  p->name = value["name"].asString();
  p->active = value["active"].asBool();
  p->primary = value["primary"].asBool();
  p->current_workspace = (current_workspace.isNull() ? std::string() : current_workspace.asString());
  p->rect = parse_rect_from_json(value["rect"]);
  return p;
}

static std::shared_ptr<binding_t> parse_binding_from_json(const Json::Value& value) {
#define i3IPC_TYPE_STR "PARSE BINDING FROM JSON"
  if (value.isNull())
    return std::shared_ptr<binding_t>();
  IPC_JSON_ASSERT_TYPE_OBJECT(value, "binding")
  std::shared_ptr<binding_t> b(new binding_t());

  b->command = value["command"].asString();
  b->symbol = value["symbol"].asString();
  b->input_code = value["input_code"].asInt();

  const std::string input_type = value["input_type"].asString();
  if (input_type == "keyboard") {
    b->input_type = InputType::KEYBOARD;
  } else if (input_type == "mouse") {
    b->input_type = InputType::MOUSE;
  } else {
    b->input_type = InputType::UNKNOWN;
  }

  const Json::Value& esm_arr = value["event_state_mask"];
  IPC_JSON_ASSERT_TYPE_ARRAY(esm_arr, "event_state_mask")

  b->event_state_mask.resize(esm_arr.size());

  for (Json::ArrayIndex i = 0; i < esm_arr.size(); i++) {
    b->event_state_mask[i] = esm_arr[i].asString();
  }

  return b;
#undef i3IPC_TYPE_STR
}

static std::shared_ptr<mode_t> parse_mode_from_json(const Json::Value& value) {
  if (value.isNull())
    return std::shared_ptr<mode_t>();

  std::shared_ptr<mode_t> p(new mode_t());
  p->change = value["change"].asString();
  p->pango_markup = value["pango_markup"].asBool();
  return p;
}

static std::shared_ptr<bar_config_t> parse_bar_config_from_json(const Json::Value& value) {
#define i3IPC_TYPE_STR "PARSE BAR CONFIG FROM JSON"
  if (value.isNull())
    return std::shared_ptr<bar_config_t>();
  IPC_JSON_ASSERT_TYPE_OBJECT(value, "(root)")
  std::shared_ptr<bar_config_t> bc(new bar_config_t());

  bc->id = value["id"].asString();
  bc->status_command = value["status_command"].asString();
  bc->font = value["font"].asString();
  bc->workspace_buttons = value["workspace_buttons"].asBool();
  bc->binding_mode_indicator = value["binding_mode_indicator"].asBool();
  bc->verbose = value["verbose"].asBool();

  std::string mode = value["mode"].asString();
  if (mode == "dock") {
    bc->mode = BarMode::DOCK;
  } else if (mode == "hide") {
    bc->mode = BarMode::HIDE;
  } else {
    bc->mode = BarMode::UNKNOWN;
    I3IPC_WARN("Got a unknown \"mode\" property: \""
               << mode
               << "\". Perhaps its neccessary to update i3ipc++. If you are using latest, note maintainer about this")
  }

  std::string position = value["position"].asString();
  if (position == "top") {
    bc->position = Position::TOP;
  } else if (position == "bottom") {
    bc->position = Position::BOTTOM;
  } else {
    bc->position = Position::UNKNOWN;
    I3IPC_WARN("Got a unknown \"position\" property: \""
               << position
               << "\". Perhaps its neccessary to update i3ipc++. If you are using latest, note maintainer about this")
  }

  const Json::Value& colors = value["colors"];
  if (!colors.isNull()) {
    IPC_JSON_ASSERT_TYPE_OBJECT(colors, "colors")
    for (const std::string& m : colors.getMemberNames()) {
      const Json::Value& color = colors[m];
      IPC_JSON_ASSERT_TYPE_STRING(color, "a color")

      // A color is a string like "#rrggbb". Validate it instead of letting std::stoul throw an unrelated exception
      // (or substr() fail) on a malformed value
      const std::string color_str = color.asString();
      const char* digits = color_str.c_str() + ((!color_str.empty() && color_str[0] == '#') ? 1 : 0);
      char* end = nullptr;
      errno = 0;
      const unsigned long parsed = strtoul(digits, &end, 16);
      if (*digits == '\0' || *end != '\0' || errno == ERANGE || parsed > 0xFFFFFFFFul) {
        throw invalid_reply_payload_error(auss_t() << "Failed to parse reply on \"" i3IPC_TYPE_STR "\": color \"" << m
                                                   << "\" has an invalid value \"" << color_str << '"');
      }
      bc->colors[m] = static_cast<uint32_t>(parsed);
    }
  }

  return bc;
#undef i3IPC_TYPE_STR
}

std::string get_socketpath() {
  // The same variable is honored by i3-msg and other i3 tools. It also allows to work without `i3` in PATH
  const char* env_path = getenv("I3SOCK");
  if (env_path != nullptr && env_path[0] != '\0') {
    return env_path;
  }

  std::string str;
  {
    auss_t str_buf;
    FILE* in;
    char buf[512] = {0};
    if (!(in = popen("i3 --get-socketpath", "r"))) {
      throw errno_error("Failed to get socket path");
    }

    while (fgets(buf, sizeof(buf), in) != nullptr) {
      str_buf << buf;
    }
    const int status = pclose(in);
    str = str_buf;
    if (status == -1) {
      throw errno_error("Failed to wait for `i3 --get-socketpath`");
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      const std::string reason = WIFEXITED(status) ? "exit code " + std::to_string(WEXITSTATUS(status))
                                                   : "signal " + std::to_string(WTERMSIG(status));
      throw ipc_error("i3 --get-socketpath failed with " + reason + " and output '" + str + "'");
    }
  }
  if (!str.empty() && str.back() == '\n') {
    str.pop_back();
  }
  return str;
}

connection::connection(const std::string& socket_path)
    : m_main_socket(i3_connect(socket_path)), m_event_socket(-1), m_subscriptions(0), m_socket_path(socket_path) {
#define i3IPC_TYPE_STR "i3's event"
  on_event = [this](EventType event_type, const std::shared_ptr<const buf_t>& buf) {
    switch (event_type) {
      case ET_WORKSPACE: {
        workspace_event_t ev;
        Json::Value root;
        IPC_JSON_READ(root);
        std::string change = root["change"].asString();
        if (change == "focus") {
          ev.type = WorkspaceEventType::FOCUS;
        } else if (change == "init") {
          ev.type = WorkspaceEventType::INIT;
        } else if (change == "empty") {
          ev.type = WorkspaceEventType::EMPTY;
        } else if (change == "urgent") {
          ev.type = WorkspaceEventType::URGENT;
        } else if (change == "rename") {
          ev.type = WorkspaceEventType::RENAME;
        } else if (change == "reload") {
          ev.type = WorkspaceEventType::RELOAD;
        } else if (change == "restored") {
          ev.type = WorkspaceEventType::RESTORED;
        } else {
          I3IPC_WARN("Unknown workspace event type " << change)
          break;
        }
        I3IPC_DEBUG("WORKSPACE " << change)

        Json::Value current = root["current"];
        Json::Value old = root["old"];

        if (!current.isNull()) {
          ev.current = parse_workspace_from_json(current);
        }
        if (!old.isNull()) {
          ev.old = parse_workspace_from_json(old);
        }

        if (on_workspace_event) {
          on_workspace_event(ev);
        }
        break;
      }
      case ET_OUTPUT:
        I3IPC_DEBUG("OUTPUT")
        if (on_output_event) {
          on_output_event();
        }
        break;
      case ET_MODE: {
        I3IPC_DEBUG("MODE")
        Json::Value root;
        IPC_JSON_READ(root);
        std::shared_ptr<mode_t> mode_data = parse_mode_from_json(root);
        if (!mode_data) {
          I3IPC_ERR("Failed to parse mode_event")
          break;
        }
        if (on_mode_event) {
          on_mode_event(*mode_data);
        }
        break;
      }
      case ET_WINDOW: {
        window_event_t ev;
        Json::Value root;
        IPC_JSON_READ(root);
        std::string change = root["change"].asString();
        if (change == "new") {
          ev.type = WindowEventType::NEW;
        } else if (change == "close") {
          ev.type = WindowEventType::CLOSE;
        } else if (change == "focus") {
          ev.type = WindowEventType::FOCUS;
        } else if (change == "title") {
          ev.type = WindowEventType::TITLE;
        } else if (change == "fullscreen_mode") {
          ev.type = WindowEventType::FULLSCREEN_MODE;
        } else if (change == "move") {
          ev.type = WindowEventType::MOVE;
        } else if (change == "floating") {
          ev.type = WindowEventType::FLOATING;
        } else if (change == "urgent") {
          ev.type = WindowEventType::URGENT;
        } else {
          // Do not pass an event with an uninitialized type to the user
          I3IPC_WARN("Unknown window event type " << change)
          break;
        }
        I3IPC_DEBUG("WINDOW " << change)

        Json::Value container = root["container"];
        if (!container.isNull()) {
          ev.container = parse_container_from_json(container);
        }

        if (on_window_event) {
          on_window_event(ev);
        }
        break;
      }
      case ET_BARCONFIG_UPDATE: {
        I3IPC_DEBUG("BARCONFIG_UPDATE")
        Json::Value root;
        IPC_JSON_READ(root);
        std::shared_ptr<bar_config_t> barconf = parse_bar_config_from_json(root);
        if (!barconf) {
          I3IPC_ERR("Failed to parse barconfig_update event")
          break;
        }
        if (on_barconfig_update_event) {
          on_barconfig_update_event(*barconf);
        }
        break;
      }
      case ET_BINDING: {
        Json::Value root;
        IPC_JSON_READ(root);
        std::string change = root["change"].asString();
        if (change != "run") {
          I3IPC_WARN("Got \"" << change << "\" in field \"change\" of binding_event. Expected \"run\"")
        }

        Json::Value binding_json = root["binding"];
        std::shared_ptr<binding_t> bptr;
        if (!binding_json.isNull()) {
          bptr = parse_binding_from_json(binding_json);
        }

        if (!bptr) {
          I3IPC_ERR("Failed to parse field \"binding\" from binding_event")
        } else {
          I3IPC_DEBUG("BINDING " << bptr->symbol);
          if (on_binding_event) {
            on_binding_event(*bptr);
          }
        }
        break;
      }
    };
  };
#undef i3IPC_TYPE_STR
}
connection::~connection() {
  i3_disconnect(m_main_socket);
  if (m_event_socket >= 0)
    this->disconnect_event_socket();
}

void connection::connect_event_socket(const bool reconnect) {
  if (m_event_socket >= 0) {
    if (reconnect) {
      this->disconnect_event_socket();
    } else {
      I3IPC_ERR("Trying to initialize event socket secondary")
      return;
    }
  }
  m_event_socket = i3_connect(m_socket_path);
  try {
    if (!this->subscribe(m_subscriptions)) {
      I3IPC_ERR("i3 refused the subscription on events")
    }
  } catch (...) {
    // Otherwise handle_event() would consider a not subscribed socket as a ready one and wait on it forever
    this->disconnect_event_socket();
    throw;
  }
}

void connection::disconnect_event_socket() {
  if (m_event_socket < 0) {
    I3IPC_WARN("Trying to disconnect non-connected event socket")
    return;
  }
  i3_disconnect(m_event_socket);
  m_event_socket = -1; // Otherwise the descriptor would be closed once more in the destructor (or reused by then)
}

bool connection::handle_event() {
  if (m_event_socket < 0) {
    this->connect_event_socket();
  }

  auto buf = i3_recv(m_event_socket);

  if (buf && this->on_event) {
    this->on_event(static_cast<EventType>(1 << (buf->header->type & 0x7f)), std::static_pointer_cast<const buf_t>(buf));
  } else if (buf) {
    return true;
  }
  return false;
}

bool connection::subscribe(const int32_t events) {
#define i3IPC_TYPE_STR "SUBSCRIBE"
  if (m_event_socket < 0) {
    m_subscriptions |= events;
    return true;
  }
  std::string payload;
  {
    auss_t payload_auss;
    if (events & static_cast<int32_t>(ET_WORKSPACE)) {
      payload_auss << "\"workspace\",";
    }
    if (events & static_cast<int32_t>(ET_OUTPUT)) {
      payload_auss << "\"output\",";
    }
    if (events & static_cast<int32_t>(ET_MODE)) {
      payload_auss << "\"mode\",";
    }
    if (events & static_cast<int32_t>(ET_WINDOW)) {
      payload_auss << "\"window\",";
    }
    if (events & static_cast<int32_t>(ET_BARCONFIG_UPDATE)) {
      payload_auss << "\"barconfig_update\",";
    }
    if (events & static_cast<int32_t>(ET_BINDING)) {
      payload_auss << "\"binding\",";
    }
    payload = payload_auss;
    if (payload.empty()) {
      return true;
    }
    payload.pop_back();
  }
  I3IPC_DEBUG("i3 IPC subscriptions: " << payload)

  auto buf = i3_msg(m_event_socket, ClientMessageType::SUBSCRIBE, auss_t() << '[' << payload << ']');
  Json::Value root;
  IPC_JSON_READ(root)

  // Remember only what i3 has really accepted, otherwise a reconnect would repeat the failing request
  if (!root["success"].asBool()) {
    return false;
  }

  m_subscriptions |= events;
  return true;
#undef i3IPC_TYPE_STR
}

version_t connection::get_version() const {
#define i3IPC_TYPE_STR "GET_VERSION"
  auto buf = i3_msg(m_main_socket, ClientMessageType::GET_VERSION);
  Json::Value root;
  IPC_JSON_READ(root)
  IPC_JSON_ASSERT_TYPE_OBJECT(root, "root")

  version_t v{};
  v.human_readable = root["human_readable"].asString();
  v.loaded_config_file_name = root["loaded_config_file_name"].asString();
  v.major = root["major"].asUInt();
  v.minor = root["minor"].asUInt();
  v.patch = root["patch"].asUInt();
  return v;
#undef i3IPC_TYPE_STR
}

std::shared_ptr<container_t> connection::get_tree() const {
#define i3IPC_TYPE_STR "GET_TREE"
  auto buf = i3_msg(m_main_socket, ClientMessageType::GET_TREE);
  Json::Value root;
  IPC_JSON_READ(root);
  return parse_container_from_json(root);
#undef i3IPC_TYPE_STR
}

std::vector<std::shared_ptr<output_t>> connection::get_outputs() const {
#define i3IPC_TYPE_STR "GET_OUTPUTS"
  auto buf = i3_msg(m_main_socket, ClientMessageType::GET_OUTPUTS);
  Json::Value root;
  IPC_JSON_READ(root)
  IPC_JSON_ASSERT_TYPE_ARRAY(root, "root")

  std::vector<std::shared_ptr<output_t>> outputs;

  for (const auto& w : root) {
    outputs.push_back(parse_output_from_json(w));
  }

  return outputs;
#undef i3IPC_TYPE_STR
}

std::vector<std::shared_ptr<workspace_t>> connection::get_workspaces() const {
#define i3IPC_TYPE_STR "GET_WORKSPACES"
  auto buf = i3_msg(m_main_socket, ClientMessageType::GET_WORKSPACES);
  Json::Value root;
  IPC_JSON_READ(root)
  IPC_JSON_ASSERT_TYPE_ARRAY(root, "root")

  std::vector<std::shared_ptr<workspace_t>> workspaces;

  for (const auto& w : root) {
    workspaces.push_back(parse_workspace_from_json(w));
  }

  return workspaces;
#undef i3IPC_TYPE_STR
}

std::vector<std::string> connection::get_bar_configs_list() const {
#define i3IPC_TYPE_STR "GET_BAR_CONFIG (get_bar_configs_list)"
  auto buf = i3_msg(m_main_socket, ClientMessageType::GET_BAR_CONFIG);
  Json::Value root;
  IPC_JSON_READ(root)
  IPC_JSON_ASSERT_TYPE_ARRAY(root, "root")

  std::vector<std::string> l;

  for (const auto& w : root) {
    l.push_back(w.asString());
  }

  return l;
#undef i3IPC_TYPE_STR
}

std::shared_ptr<bar_config_t> connection::get_bar_config(const std::string& name) const {
#define i3IPC_TYPE_STR "GET_BAR_CONFIG"
  auto buf = i3_msg(m_main_socket, ClientMessageType::GET_BAR_CONFIG, name);
  Json::Value root;
  IPC_JSON_READ(root)
  return parse_bar_config_from_json(root);
#undef i3IPC_TYPE_STR
}

bool connection::send_command(const std::string& command) const {
#define i3IPC_TYPE_STR "COMMAND"
  auto buf = i3_msg(m_main_socket, ClientMessageType::COMMAND, command);
  Json::Value root;
  IPC_JSON_READ(root)
  IPC_JSON_ASSERT_TYPE_ARRAY(root, "root")
  if (root.empty()) {
    throw invalid_reply_payload_error("Failed to parse reply on \"" i3IPC_TYPE_STR "\": root is expected to be non-empty");
  }

  // i3 sends a result per command: "workspace 1; move left" produces two of them. The command line is successful only
  // if all of them are
  bool success = true;
  for (Json::ArrayIndex i = 0; i < root.size(); i++) {
    const Json::Value& payload = root[i];
    IPC_JSON_ASSERT_TYPE_OBJECT(payload, "an item of root")

    if (!payload["success"].asBool()) {
      success = false;
      const Json::Value& error = payload["error"];
      if (!error.isNull()) {
        I3IPC_ERR("Failed to execute command: " << error.asString())
      }
    }
  }
  return success;
#undef i3IPC_TYPE_STR
}

int32_t connection::get_main_socket_fd() {
  return m_main_socket;
}

int32_t connection::get_event_socket_fd() {
  return m_event_socket;
}

const version_t& get_version() {
#define I3IPC_VERSION_MAJOR 0
#define I3IPC_VERSION_MINOR 5
#define I3IPC_VERSION_PATCH 0
  // Initialized once (thread-safe since C++11) instead of being rewritten by every call
  static const version_t v = []() {
    version_t ver{};
    ver.human_readable = auss_t() << I3IPC_VERSION_MAJOR << '.' << I3IPC_VERSION_MINOR << '.' << I3IPC_VERSION_PATCH;
    ver.loaded_config_file_name = std::string();
    ver.major = I3IPC_VERSION_MAJOR;
    ver.minor = I3IPC_VERSION_MINOR;
    ver.patch = I3IPC_VERSION_PATCH;
    return ver;
  }();
  return v;
}

} // namespace i3ipc
