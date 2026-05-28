import shutil
import os
from datetime import datetime

Import("env")

def archive_firmware(source, target, env):
    firmware_src = os.path.join(env.subst("$BUILD_DIR"), "firmware.bin")
    archive_dir  = os.path.join(env.subst("$PROJECT_DIR"), "firmware_archive")
    
    os.makedirs(archive_dir, exist_ok=True)
    
    # Kopie mit Timestamp anlegen
    timestamp   = datetime.now().strftime("%Y%m%d_%H%M%S")
    destination = os.path.join(archive_dir, f"firmware_{timestamp}.bin")
    shutil.copy(firmware_src, destination)
    print(f"Firmware archiviert: {destination}")
    
    # Nur die letzten 3 behalten
    files = sorted(
        [f for f in os.listdir(archive_dir) if f.endswith(".bin")],
    )
    while len(files) > 5:
        oldest = os.path.join(archive_dir, files.pop(0))
        os.remove(oldest)
        print(f"Alte Firmware gelöscht: {oldest}")

env.AddPostAction("$BUILD_DIR/firmware.bin", archive_firmware)