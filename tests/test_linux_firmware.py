import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    'linux_firmware', Path(__file__).resolve().parents[1] / 'tools/linux_firmware.py')
fw = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(fw)


class FirmwareTests(unittest.TestCase):
    def test_map(self):
        self.assertEqual(len(fw.FILES), 16)
        self.assertEqual(len(fw.OFFSETS), 16)
        self.assertEqual(len(fw.APPS), 12)
        self.assertEqual(fw.OFFSETS[-1], '0xB00000')

    def test_missing_rejected(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(fw, 'ROOT', Path(tmp)):
            with self.assertRaisesRegex(RuntimeError, 'Missing'):
                fw.inputs('lcd')

    def test_variant_isolation_and_empty_rejected(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(fw, 'ROOT', Path(tmp)):
            bins, image = fw.locations('hdmi')
            bins.mkdir()
            for name in fw.FILES:
                (bins / name).write_bytes(b'x')
            self.assertEqual(len(fw.inputs('hdmi')), 32)
            with self.assertRaises(RuntimeError):
                fw.inputs('lcd')
            (bins / fw.FILES[0]).write_bytes(b'')
            with self.assertRaisesRegex(RuntimeError, 'Empty'):
                fw.inputs('hdmi')

    def test_failed_merge_keeps_previous_image(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(fw, 'ROOT', Path(tmp)):
            bins, image = fw.locations('lcd')
            bins.mkdir()
            for name in fw.FILES:
                (bins / name).write_bytes(b'x')
            image.write_bytes(b'previous')
            with patch.object(fw, 'run', side_effect=RuntimeError('failed')):
                with self.assertRaises(RuntimeError):
                    fw.merge('lcd')
            self.assertEqual(image.read_bytes(), b'previous')

    def test_build_failure_stops_before_merge(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(fw, 'ROOT', Path(tmp)), \
             patch.object(fw.shutil, 'which', return_value='/idf/idf.py'), \
             patch.dict(fw.os.environ, {'IDF_PATH': '/idf'}), \
             patch.object(fw, 'run', side_effect=RuntimeError('build failed')), \
             patch.object(fw, 'merge') as merge:
            with self.assertRaises(RuntimeError):
                fw.build('lcd')
            merge.assert_not_called()


if __name__ == '__main__':
    unittest.main()
