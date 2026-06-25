Import("env")
import os

env_file = os.path.join(env["PROJECT_DIR"], ".env")
if not os.path.exists(env_file):
    print("WARNING: hardware/.env not found — WIFI_SSID and WIFI_PASSWORD will be undefined")
else:
    with open(env_file) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, _, val = line.partition("=")
            env.Append(BUILD_FLAGS=[f'-D{key.strip()}=\\"{val.strip()}\\"'])
