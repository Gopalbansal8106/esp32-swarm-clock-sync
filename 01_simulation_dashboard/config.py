"""
Simulation Configuration
Customize these parameters to adjust simulation behavior
"""

# ============================================================================
# NETWORK PARAMETERS
# ============================================================================

# Number of nodes at startup (2-20 recommended)
INITIAL_NODES = 4

# Maximum number of nodes allowed
MAX_NODES = 20

# Network latency mean (milliseconds)
LATENCY_MEAN_MS = 20

# Network latency standard deviation (milliseconds)
LATENCY_STD_MS = 5

# Initial packet loss rate (0.0 - 1.0)
INITIAL_PACKET_LOSS_RATE = 0.01  # 1%

# ============================================================================
# CLOCK DRIFT PARAMETERS
# ============================================================================

# Initial drift rate range (parts per million)
# Negative = slower than reference, Positive = faster
DRIFT_RATE_MIN = -2000  # PPM
DRIFT_RATE_MAX = 2000   # PPM

# Initial clock offset range (seconds)
CLOCK_OFFSET_MIN = -5   # seconds
CLOCK_OFFSET_MAX = 5    # seconds

# ============================================================================
# SYNCHRONIZATION PARAMETERS
# ============================================================================

# Convergence speed (0.0 = very slow, 1.0 = instant)
# Lower values = more realistic gradual sync
CONVERGENCE_SPEED = 0.2

# Error threshold for considering node synchronized (seconds)
SYNC_ERROR_THRESHOLD = 0.001  # 1 millisecond

# ============================================================================
# SIMULATION TIMING
# ============================================================================

# Simulation update timestep (seconds)
# Typical: 0.016 for 60 FPS
SIMULATION_DT = 0.016

# Maximum allowed timestep (prevents jumps)
MAX_DT = 0.05  # seconds

# WebSocket broadcast frequency (Hz)
BROADCAST_FREQUENCY = 60  # Updates per second

# ============================================================================
# FAULTY NODE BEHAVIOR
# ============================================================================

# Extreme drift rates for faulty nodes (PPM)
FAULTY_DRIFT_MIN = -500
FAULTY_DRIFT_MAX = 500

# ============================================================================
# SERVER CONFIGURATION
# ============================================================================

# Server host
SERVER_HOST = "0.0.0.0"  # 0.0.0.0 = accessible from any machine

# Server port
SERVER_PORT = 8000

# Enable auto-reload (development)
ENABLE_RELOAD = False

# Log level (info, debug, warning, error)
LOG_LEVEL = "info"

# ============================================================================
# FRONTEND CONFIGURATION
# ============================================================================

# Chart history size (number of data points kept)
CHART_HISTORY_SIZE = 100

# Topology canvas update rate (Hz)
TOPOLOGY_UPDATE_RATE = 60

# Animation speed multiplier (1.0 = normal)
ANIMATION_SPEED = 1.0

# ============================================================================
# TESTING/DEBUG
# ============================================================================

# Enable debug logging
DEBUG = False

# Log all WebSocket messages
LOG_WEBSOCKET = False

# Simulate network degradation (0.0 - 1.0)
NETWORK_DEGRADATION = 0.0

# ============================================================================
# THEME CUSTOMIZATION
# ============================================================================

# UI Colors (hex format)
COLORS = {
    'primary_bg': '#0a0e27',
    'secondary_bg': '#1a1f3a',
    'accent': '#00ff88',
    'accent_light': '#00ff99',
    'accent_dim': '#008844',
    'error': '#ff0055',
    'warning': '#ffaa00',
    'success': '#00ff88',
    'master': '#00ffaa',
    'text_primary': '#ffffff',
    'text_secondary': '#b0b0b0',
}

# Animation keyframes
ANIMATIONS = {
    'pulse_duration': 2.0,      # seconds
    'shimmer_duration': 2.0,    # seconds
    'glow_duration': 2.0,       # seconds
    'packet_travel_time': 0.5,  # seconds
}
