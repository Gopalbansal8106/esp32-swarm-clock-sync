import sys
import os

# Set standard output encoding to UTF-8 to prevent Windows UnicodeEncodeError
try:
    sys.stdout.reconfigure(encoding='utf-8')
    sys.stderr.reconfigure(encoding='utf-8')
except Exception:
    pass

import threading
import uvicorn
import main

# Target COM Port
com_port = "COM16"

# Start background Serial-to-Websocket bridge thread
t = threading.Thread(target=main.run_serial_reader, args=(com_port,), daemon=True)
t.start()

print(f"[*] Hardware Monitor Bridge started on {com_port}")
print("Status: Launching Dashboard on http://localhost:8090...")

# Run web app
uvicorn.run(main.app, host="0.0.0.0", port=8090, log_level="warning")
