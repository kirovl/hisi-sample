"""Validate runtime contents and reject incomplete/mismatched vendor inputs."""
import importlib.util
import pathlib
import subprocess
import sys
import tempfile
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parents[1] / 'wifi-provision/package-runtime.py'
spec = importlib.util.spec_from_file_location('runtime', SCRIPT)
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)


class RuntimeTests(unittest.TestCase):
    def test_package_and_rejected_replacement(self):
        with tempfile.TemporaryDirectory() as tmp:
            src, out = pathlib.Path(tmp) / 'src', pathlib.Path(tmp) / 'runtime'
            for rel in runtime.FILES:
                path = src / rel
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'test-file')
            for rel, _ in runtime.MODULES.values():
                path = src / rel
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'vermagic=5.10.221 SMP mod_unload ARMv7\0')
            firmware = src / 'ws73'
            firmware.mkdir()
            for name in ('btc_cali.bin', 'wifi_cali.bin', 'wow.bin', 'ws73.bin', 'ws73_cfg.ini'):
                (firmware / name).write_bytes(b'firmware')

            def run():
                return subprocess.run([sys.executable, str(SCRIPT), str(src), str(out)],
                                      capture_output=True, text=True)

            result = run()
            self.assertEqual(result.returncode, 0, result.stderr)
            conf = out / 'etc/wireless/wpa_supplicant.conf'
            self.assertEqual(conf.stat().st_mode & 0o777, 0o600)
            self.assertNotIn('network=', conf.read_text())
            self.assertTrue((out / 'usr/share/udhcpc/default.script').stat().st_mode & 0o111)
            original = (out / 'SHA256SUMS').read_bytes()
            (src / 'root/wifi_soc.ko').write_bytes(b'vermagic=5.10.221+ SMP\0')
            self.assertNotEqual(run().returncode, 0)
            self.assertEqual((out / 'SHA256SUMS').read_bytes(), original)
            (firmware / 'ws73.bin').unlink()
            self.assertNotEqual(run().returncode, 0)
            self.assertEqual((out / 'SHA256SUMS').read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
