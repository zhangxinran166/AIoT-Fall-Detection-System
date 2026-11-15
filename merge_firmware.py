# Merge PlatformIO's bootloader + partition table + application into one image.
# Wokwi loads this merged image so the ESP32-S3 sees the same flash layout as the real board.
Import("env")
from os.path import join

def merge_firmware(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    merged = join(build_dir, "firmware.merged.bin")
    esptool = join(
        env.PioPlatform().get_package_dir("tool-esptoolpy"),
        "esptool.py",
    )

    cmd = [
        '"$PYTHONEXE"',
        '"%s"' % esptool,
        "--chip", "esp32s3",
        "merge_bin",
        "-o", '"%s"' % merged,
        "--flash_mode", "dio",
        "--flash_size", "8MB",
        "0x0", '"%s"' % join(build_dir, "bootloader.bin"),
        "0x8000", '"%s"' % join(build_dir, "partitions.bin"),
        "0x10000", '"%s"' % str(target[0]),
    ]
    env.Execute(env.VerboseAction(" ".join(cmd), "Merging firmware for Wokwi"))

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_firmware)
