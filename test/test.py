import os
import pathlib
import platform
import pytest
import shutil
import subprocess
import yaml


def is_windows():
    return platform.system() == "Windows"


class CaseModel:

    KEYWORD_CMD = "cmd"
    KEYWORD_ASSERT = "assert"

    def __init__(self, name, cmd, setup_files, assert_files):
        self.name = name
        self.cmd = cmd
        self.setup_files = setup_files
        self.assert_files = assert_files

    @classmethod
    def parse(cls, yml_path):
        with open(yml_path, 'r') as f:
            test_config = yaml.safe_load(f)
        name = ".".join(pathlib.Path(yml_path).relative_to(Test.CASES_PATH).with_suffix("").parts)
        cmd = test_config.get("cmd")
        setup_files = cls.normalize({k: v for k, v in test_config.items() if k not in [cls.KEYWORD_CMD, cls.KEYWORD_ASSERT]})
        assert_files = cls.normalize(test_config.get(cls.KEYWORD_ASSERT, {}))
        return cls(name, cmd, setup_files, assert_files)

    @classmethod
    def normalize(cls, dict):
        return {k: "\n".join(cls.to_array(v)) for k, v in dict.items()}

    @staticmethod
    def to_array(value):
        if value == None:
            return []
        if isinstance(value, str):
            return [value]
        return value


class Test:

    PROGRAM_NAME = "findany"
    CASES_PATH = "cases"
    TMP_PATH = "tmp"
    BUILD_PATH = os.path.join("..", "build")

    def setup_method(self):
        shutil.rmtree(self.TMP_PATH, ignore_errors=True)
        shutil.copytree(self.BUILD_PATH, self.TMP_PATH)
        if not is_windows():
            subprocess.run(["chmod", "+x", os.path.join(self.TMP_PATH, self.PROGRAM_NAME)])

    @staticmethod
    def get_cases():
        for (dirpath, _, filenames) in os.walk(Test.CASES_PATH):
            for filename in filenames:
                yield CaseModel.parse(os.path.join(dirpath, filename))

    @pytest.mark.parametrize("case", get_cases(), ids=lambda case: case.name)
    def test(self, case):
        for name, content in case.setup_files.items():
            self.write_tmp_file(name, content)
        cmd = case.cmd
        if is_windows():
            cmd = cmd.replace(self.PROGRAM_NAME, f"{self.PROGRAM_NAME}.exe")
        else:
            cmd = cmd.replace(self.PROGRAM_NAME, f"./{self.PROGRAM_NAME}")

        subprocess.Popen(cmd, shell=True, cwd=self.TMP_PATH).wait()

        assert len(case.assert_files) > 0
        for name, content in case.assert_files.items():
            actual = self.read_tmp_file(name)
            assert content == actual

    def write_tmp_file(self, name, content):
        with open(os.path.join(self.TMP_PATH, name), 'w') as f:
            f.write(content)

    def read_tmp_file(self, name):
        with open(os.path.join(self.TMP_PATH, name), 'r') as f:
            return f.read()
