"""
Unreal Engine MCP Server

A simple MCP server for interacting with Unreal Engine.
"""

import logging
from logging.handlers import RotatingFileHandler
import socket
import json
import os
import threading
import time
from contextlib import asynccontextmanager
from typing import AsyncIterator, Dict, Any, Optional, Tuple
from mcp.server.fastmcp import FastMCP

# Keep diagnostics bounded and avoid serializing large MCP payloads by default.
LOG_LEVEL_NAME = os.environ.get("UNREAL_MCP_LOG_LEVEL", "INFO").upper()
LOG_PAYLOADS = os.environ.get("UNREAL_MCP_LOG_PAYLOADS", "0") == "1"
log_handler = RotatingFileHandler(
    "unreal_mcp.log",
    maxBytes=4 * 1024 * 1024,
    backupCount=2,
    encoding="utf-8",
    delay=True,
)
logging.basicConfig(
    level=getattr(logging, LOG_LEVEL_NAME, logging.INFO),
    format='%(asctime)s - %(name)s - %(levelname)s - [%(filename)s:%(lineno)d] - %(message)s',
    handlers=[log_handler],
)
logger = logging.getLogger("UnrealMCP")

# Configuration
UNREAL_HOST = "127.0.0.1"
UNREAL_PORT = int(os.environ.get("UNREAL_MCP_PORT", "55557"))
SOCKET_TIMEOUT_SECONDS = float(os.environ.get("UNREAL_MCP_SOCKET_TIMEOUT", "10"))
MAX_RESPONSE_BYTES = int(os.environ.get("UNREAL_MCP_MAX_RESPONSE_BYTES", str(64 * 1024 * 1024)))

class UnrealConnection:
    """Connection to an Unreal Engine instance."""
    
    def __init__(self):
        """Initialize the connection."""
        self.socket = None
        self.connected = False
        self._lock = threading.RLock()
    
    def connect(self) -> bool:
        """Connect to the Unreal Engine instance."""
        try:
            # Close any existing socket
            if self.socket:
                try:
                    self.socket.close()
                except:
                    pass
                self.socket = None
            
            logger.debug("Connecting to Unreal at %s:%d", UNREAL_HOST, UNREAL_PORT)
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(SOCKET_TIMEOUT_SECONDS)
            
            # Set socket options for better stability
            self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
            
            # Set larger buffer sizes
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 65536)
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 65536)
            
            self.socket.connect((UNREAL_HOST, UNREAL_PORT))
            self.connected = True
            return True
            
        except Exception as e:
            logger.error("Failed to connect to Unreal: %s", e)
            self.connected = False
            return False
    
    def disconnect(self):
        """Disconnect from the Unreal Engine instance."""
        with self._lock:
            if self.socket:
                try:
                    self.socket.close()
                except OSError:
                    pass
            self.socket = None
            self.connected = False

    def receive_full_response(self, sock, buffer_size=65536) -> Tuple[bytes, int]:
        """Read one response to EOF without repeatedly joining or parsing prefixes."""
        data = bytearray()
        chunk_count = 0
        sock.settimeout(SOCKET_TIMEOUT_SECONDS)
        try:
            while True:
                chunk = sock.recv(buffer_size)
                if not chunk:
                    if not data:
                        raise Exception("Connection closed before receiving data")
                    break
                data.extend(chunk)
                chunk_count += 1
                if len(data) > MAX_RESPONSE_BYTES:
                    raise ValueError(f"Response exceeds {MAX_RESPONSE_BYTES} byte limit")
        except socket.timeout:
            # Compatibility with an older Editor plugin that did not close the
            # response side of the connection. New plugins always frame by EOF.
            if data:
                try:
                    json.loads(bytes(data).decode("utf-8"))
                    logger.warning("Accepted legacy unframed response after socket timeout")
                    return bytes(data), chunk_count
                except (UnicodeDecodeError, json.JSONDecodeError):
                    pass
            raise Exception("Timeout receiving Unreal response")
        except Exception as e:
            logger.error("Error during receive: %s", e)
            raise

        return bytes(data), chunk_count
    
    def send_command(self, command: str, params: Dict[str, Any] = None) -> Optional[Dict[str, Any]]:
        """Send a command to Unreal Engine and get the response."""
        with self._lock:
            command_obj = {"type": command, "params": params or {}}
            command_json = json.dumps(command_obj, ensure_ascii=False, separators=(",", ":"))
            command_payload = command_json.encode("utf-8")
            started_at = time.perf_counter()

            if self.socket:
                self.disconnect()

            connect_started_at = time.perf_counter()
            if not self.connect():
                self.disconnect()
                logger.error("Failed to connect to Unreal Engine for command=%s", command)
                return None
            connect_ms = (time.perf_counter() - connect_started_at) * 1000.0

            try:
                if LOG_PAYLOADS:
                    logger.debug("request command=%s payload=%s", command, command_json)

                send_started_at = time.perf_counter()
                self.socket.sendall(command_payload)
                send_ms = (time.perf_counter() - send_started_at) * 1000.0

                receive_started_at = time.perf_counter()
                response_data, chunk_count = self.receive_full_response(self.socket)
                receive_ms = (time.perf_counter() - receive_started_at) * 1000.0

                decode_started_at = time.perf_counter()
                response = json.loads(response_data.decode("utf-8"))
                decode_ms = (time.perf_counter() - decode_started_at) * 1000.0

                if LOG_PAYLOADS:
                    logger.debug("response command=%s payload=%s", command, response)

                if response.get("status") == "error":
                    error_message = response.get("error") or response.get("message", "Unknown Unreal error")
                    logger.error("Unreal command=%s error=%s", command, error_message)
                    response.setdefault("error", error_message)
                elif response.get("success") is False:
                    error_message = response.get("error") or response.get("message", "Unknown Unreal error")
                    logger.error("Unreal command=%s error=%s", command, error_message)
                    response = {"status": "error", "error": error_message}

                total_ms = (time.perf_counter() - started_at) * 1000.0
                logger.info(
                    "command=%s status=%s request_bytes=%d response_bytes=%d chunks=%d "
                    "connect_ms=%.2f send_ms=%.2f receive_ms=%.2f decode_ms=%.2f total_ms=%.2f",
                    command,
                    response.get("status", "unknown"),
                    len(command_payload),
                    len(response_data),
                    chunk_count,
                    connect_ms,
                    send_ms,
                    receive_ms,
                    decode_ms,
                    total_ms,
                )
                return response
            except Exception as e:
                logger.error("Error sending command=%s: %s", command, e)
                return {"status": "error", "error": str(e)}
            finally:
                self.disconnect()

# Global connection state
_unreal_connection: UnrealConnection = None

def get_unreal_connection() -> Optional[UnrealConnection]:
    """Get the connection to Unreal Engine."""
    global _unreal_connection
    try:
        if _unreal_connection is None:
            _unreal_connection = UnrealConnection()
        return _unreal_connection
    except Exception as e:
        logger.error("Error getting Unreal connection: %s", e)
        return None

@asynccontextmanager
async def server_lifespan(server: FastMCP) -> AsyncIterator[Dict[str, Any]]:
    """Handle server startup and shutdown."""
    global _unreal_connection
    logger.info("UnrealMCP server starting up")
    try:
        _unreal_connection = get_unreal_connection()
        if _unreal_connection:
            logger.info("Unreal connection manager initialized")
        else:
            logger.warning("Could not initialize Unreal connection manager")
    except Exception as e:
        logger.error("Error initializing Unreal connection manager: %s", e)
        _unreal_connection = None
    
    try:
        yield {}
    finally:
        if _unreal_connection:
            _unreal_connection.disconnect()
            _unreal_connection = None
        logger.info("Unreal MCP server shut down")

# Initialize server
mcp = FastMCP(
    "UnrealMCP",
    description="Unreal Engine integration via Model Context Protocol",
    lifespan=server_lifespan
)

# Import and register tools
from tools.editor_tools import register_editor_tools
from tools.blueprint_tools import register_blueprint_tools
from tools.node_tools import register_blueprint_node_tools
from tools.project_tools import register_project_tools
from tools.umg_tools import register_umg_tools
from tools.animation_tools import register_animation_tools
from tools.input_tools import register_input_tools
from tools.mesh_tools import register_mesh_tools
from tools.statetree_tools import register_statetree_tools
from tools.data_asset_tools import register_data_asset_tools
from tools.material_tools import register_material_tools
from tools.spline_tools import register_spline_tools
from tools.niagara_tools import register_niagara_tools
from tools.gameplay_effect_tools import register_gameplay_effect_tools
from tools.python_tools import register_python_tools
from tools.datatable_tools import register_datatable_tools  # Phase D 2026-06-24

# Register tools
register_editor_tools(mcp)
register_blueprint_tools(mcp)
register_blueprint_node_tools(mcp)
register_project_tools(mcp)
register_umg_tools(mcp)
register_animation_tools(mcp)
register_input_tools(mcp)
register_mesh_tools(mcp)
register_statetree_tools(mcp)
register_data_asset_tools(mcp)
register_material_tools(mcp)
register_spline_tools(mcp)
register_niagara_tools(mcp)
register_gameplay_effect_tools(mcp)
register_python_tools(mcp)
register_datatable_tools(mcp)  # Phase D 2026-06-24

@mcp.prompt()
def info():
    """Information about available Unreal MCP tools and best practices."""
    return """
    # Unreal MCP Server Tools and Best Practices
    
    ## UMG (Widget Blueprint) Tools
    - `create_umg_widget_blueprint(widget_name, parent_class="UserWidget", path="/Game/UI")` 
      Create a new UMG Widget Blueprint
    - `add_text_block_to_widget(widget_name, text_block_name, text="", position=[0,0], size=[200,50], font_size=12, color=[1,1,1,1])`
      Add a Text Block widget with customizable properties
    - `add_button_to_widget(widget_name, button_name, text="", position=[0,0], size=[200,50], font_size=12, color=[1,1,1,1], background_color=[0.1,0.1,0.1,1])`
      Add a Button widget with text and styling
    - `bind_widget_event(widget_name, widget_component_name, event_name, function_name="")`
      Bind events like OnClicked to functions
    - `add_widget_to_viewport(widget_name, z_order=0)`
      Add widget instance to game viewport
    - `set_text_block_binding(widget_name, text_block_name, binding_property, binding_type="Text")`
      Set up dynamic property binding for text blocks

    ## Editor Tools
    ### Viewport and Screenshots
    - `focus_viewport(target, location, distance, orientation)` - Focus viewport
    - `take_screenshot(filename, show_ui, resolution)` - Capture screenshots

    ### Actor Management
    - `get_actors_in_level()` - List all actors in current level
    - `find_actors_by_name(pattern)` - Find actors by name pattern
    - `spawn_actor(name, type, location=[0,0,0], rotation=[0,0,0], scale=[1,1,1])` - Create actors
    - `delete_actor(name)` - Remove actors
    - `set_actor_transform(name, location, rotation, scale)` - Modify actor transform
    - `get_actor_properties(name)` - Get actor properties
    
    ## Blueprint Management
    - `create_blueprint(name, parent_class)` - Create new Blueprint classes
    - `add_component_to_blueprint(blueprint_name, component_type, component_name)` - Add components
    - `set_static_mesh_properties(blueprint_name, component_name, static_mesh)` - Configure meshes
    - `set_physics_properties(blueprint_name, component_name)` - Configure physics
    - `compile_blueprint(blueprint_name)` - Compile Blueprint changes
    - `set_blueprint_property(blueprint_name, property_name, property_value)` - Set properties
    - `set_pawn_properties(blueprint_name)` - Configure Pawn settings
    - `spawn_blueprint_actor(blueprint_name, actor_name)` - Spawn Blueprint actors
    
    ## Blueprint Node Management
    - `add_blueprint_event_node(blueprint_name, event_type)` - Add event nodes
    - `add_blueprint_input_action_node(blueprint_name, action_name)` - Add input nodes
    - `add_blueprint_function_node(blueprint_name, target, function_name)` - Add function nodes
    - `connect_blueprint_nodes(blueprint_name, source_node_id, source_pin, target_node_id, target_pin)` - Connect nodes
    - `add_blueprint_variable(blueprint_name, variable_name, variable_type)` - Add variables
    - `add_blueprint_get_self_component_reference(blueprint_name, component_name)` - Add component refs
    - `add_blueprint_self_reference(blueprint_name)` - Add self references
    - `find_blueprint_nodes(blueprint_name, node_type, event_type)` - Find nodes
    
    ## Project Tools
    - `create_input_mapping(action_name, key, input_type)` - Create input mappings
    - `batch_read(operations, stop_on_error=False)` - Combine 1-8 independent allowlisted reads

    Use `batch_read` only when intermediate results do not change the next read.
    Keep writes, approval-sensitive actions, and adaptive investigation as direct calls.
    
    ## Best Practices
    
    ### UMG Widget Development
    - Create widgets with descriptive names that reflect their purpose
    - Use consistent naming conventions for widget components
    - Organize widget hierarchy logically
    - Set appropriate anchors and alignment for responsive layouts
    - Use property bindings for dynamic updates instead of direct setting
    - Handle widget events appropriately with meaningful function names
    - Clean up widgets when no longer needed
    - Test widget layouts at different resolutions
    
    ### Editor and Actor Management
    - Use unique names for actors to avoid conflicts
    - Clean up temporary actors
    - Validate transforms before applying
    - Check actor existence before modifications
    - Take regular viewport screenshots during development
    - Keep the viewport focused on relevant actors during operations
    
    ### Blueprint Development
    - Compile Blueprints after changes
    - Use meaningful names for variables and functions
    - Organize nodes logically
    - Test functionality in isolation
    - Consider performance implications
    - Document complex setups
    
    ### Error Handling
    - Check command responses for success
    - Handle errors gracefully
    - Log important operations
    - Validate parameters
    - Clean up resources on errors
    """

# Run the server
if __name__ == "__main__":
    logger.info("Starting MCP server with stdio transport")
    mcp.run(transport='stdio')
