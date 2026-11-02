#!/usr/bin/env python3
"""Linux firmware build/merge/flash driver. Source ESP-IDF export.sh first."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
APPS = ('nes', 'gb', 'sms', 'spectrum', 'stella', 'prosystem', 'handy',
        'pce', 'atari800', 'snes', 'genesis', 'neogeo')
OFFSETS = ('0x2000', '0x8000', '0xD000', '0x10000', '0x0D0000',
           '0x160000', '0x200000', '0x350000', '0x410000', '0x550000',
           '0x5F0000', '0x690000', '0x740000', '0x8C0000', '0x9B0000', '0xB00000')
FILES = ('bootloader.bin', 'partition-table.bin', 'ota_data_initial.bin',
         'launcher.bin') + tuple(app + '_app.bin' for app in APPS)


def run(args, cwd=None):
    print('+', ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=cwd, check=True)


def locations(variant):
    bins = ROOT / ('firmware_hdmi' if variant == 'hdmi' else 'firmware')
    image = ROOT / ('RetroESP32_P4_HDMI_v1.bin' if variant == 'hdmi'
                    else 'RetroESP32_P4_v1.bin')
    return bins, image


def build(variant, clean=False):
    idf = shutil.which('idf.py')
    if not idf or not os.environ.get('IDF_PATH'):
        raise RuntimeError('Source your ESP-IDF export.sh before building.')
    bins, _ = locations(variant)
    bins.mkdir(exist_ok=True)
    for app in ('launcher',) + APPS:
        project = ROOT / ('launcher' if app == 'launcher' else 'apps/' + app)
        work = project / ('build_linux_' + variant)
        config = project / ('sdkconfig.linux.' + variant)
        if clean:
            if work.exists():
                shutil.rmtree(work)
            config.unlink(missing_ok=True)
        defaults = []
        if (project / 'sdkconfig.defaults').is_file():
            defaults.append(str(project / 'sdkconfig.defaults'))
        if variant == 'hdmi':
            defaults.append(str(ROOT / 'launcher/sdkconfig.hdmi.defaults'))
        # Applied last so it wins: ESP-IDF 5.4 ignores CONFIG_SPIRAM_SPEED_200M
        # (falling back to 20 MHz) unless experimental features are enabled.
        overlay = ROOT / 'sdkconfig.linux.defaults'
        if overlay.is_file():
            defaults.append(str(overlay))
        run([idf, '-B', work, '-DIDF_TARGET=esp32p4',
             '-DSDKCONFIG=' + str(config),
             '-DSDKCONFIG_DEFAULTS=' + ';'.join(defaults), 'build'], cwd=project)
        name = 'launcher.bin' if app == 'launcher' else app + '_app.bin'
        shutil.copy2(work / name, bins / name)
        if app == 'launcher':
            for source, target in (
                ('bootloader/bootloader.bin', 'bootloader.bin'),
                ('partition_table/partition-table.bin', 'partition-table.bin'),
                ('ota_data_initial.bin', 'ota_data_initial.bin')):
                shutil.copy2(work / source, bins / target)
    merge(variant)


def inputs(variant):
    bins, _ = locations(variant)
    missing = [name for name in FILES if not (bins / name).is_file()]
    if missing:
        raise RuntimeError('Missing firmware binaries: ' + ', '.join(missing))
    args = []
    for offset, name in zip(OFFSETS, FILES):
        path = bins / name
        if not path.stat().st_size:
            raise RuntimeError('Empty firmware binary: ' + str(path))
        args.extend([offset, str(path)])
    return args


def merge(variant):
    args = inputs(variant)
    _, image = locations(variant)
    temporary = image.with_suffix('.bin.tmp')
    try:
        run([sys.executable, '-m', 'esptool', '--chip', 'esp32p4',
             'merge_bin', '--output', temporary, '--flash_mode', 'dio',
             '--flash_size', '16MB', '--flash_freq', '80m'] + args)
        temporary.replace(image)
    finally:
        temporary.unlink(missing_ok=True)
    print('Merged firmware:', image)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('build', 'merge', 'flash'))
    parser.add_argument('--variant', choices=('lcd', 'hdmi'), default='lcd')
    parser.add_argument('--clean', action='store_true',
                        help='Reset only the selected Linux build/config directories')
    parser.add_argument('--port', default=os.environ.get('ESP_PORT'))
    parser.add_argument('--baud', type=int, default=460800)
    args = parser.parse_args()
    if args.clean and args.action != 'build':
        parser.error('--clean applies only to build')
    if args.action == 'flash' and not args.port:
        parser.error('flash requires --port or ESP_PORT (no guessed serial port)')
    try:
        if args.action == 'build':
            build(args.variant, args.clean)
        elif args.action == 'merge':
            merge(args.variant)
        else:
            binaries = inputs(args.variant)
            run([sys.executable, '-m', 'esptool', '--chip', 'esp32p4',
                 '-p', args.port, '-b', args.baud, 'write_flash',
                 '--flash_mode', 'dio', '--flash_size', '16MB',
                 '--flash_freq', '80m'] + binaries)
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print('ERROR:', error, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
