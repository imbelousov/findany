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

    def __init__(self, name, substrings, input, output, args):
        self.name = name
        self.substrings = substrings
        self.input = input
        self.output = output
        self.args = args

    @classmethod
    def parse(cls, yml_path):
        with open(yml_path, 'r') as f:
            test_config = yaml.safe_load(f)
        name = ".".join(pathlib.Path(yml_path).relative_to(Test.CASES_PATH).with_suffix("").parts)
        substrings = cls.join(cls.to_array(test_config.get("substrings", [])))
        input = cls.join(cls.to_array(test_config.get("input", [])))
        output = cls.join(cls.to_array(test_config.get("output", [])))
        args = cls.to_array(test_config.get("args", []))
        return cls(name, substrings, input, output, args)

    @staticmethod
    def to_array(value):
        if value == None:
            return []
        if isinstance(value, str):
            return [value]
        return value
    
    @staticmethod
    def join(lines):
        return "\n".join(lines)


class Test:

    CASES_PATH = "cases"
    TMP_PATH = "tmp"
    BUILD_PATH = os.path.join("..", "build")

    def setup_method(self):
        shutil.rmtree(self.TMP_PATH, ignore_errors=True)
        shutil.copytree(self.BUILD_PATH, self.TMP_PATH)
        if not is_windows():
            subprocess.run(["chmod", "+x", os.path.join(self.TMP_PATH, "findany")])

    @staticmethod
    def get_cases():
        for (dirpath, _, filenames) in os.walk(Test.CASES_PATH):
            for filename in filenames:
                yield CaseModel.parse(os.path.join(dirpath, filename))

    @pytest.mark.parametrize("case", get_cases(), ids=lambda case: case.name)
    def test(self, case):
        has_substrings = len(case.substrings) > 0
        self.write_tmp_file("input", case.input)
        if has_substrings:
            self.write_tmp_file("substrings", case.substrings)
        binpath = "findany.exe" if is_windows() else "./findany"
        cmd = f"{binpath} {" ".join(case.args)} {"substrings" if has_substrings else ""} < input > output"
        subprocess.Popen(cmd, shell=True, cwd=self.TMP_PATH).wait()
        actual = self.read_tmp_file("output")
        assert case.output == actual

    def write_tmp_file(self, name, content):
        with open(os.path.join(self.TMP_PATH, name), 'w') as f:
            f.write(content)

    def read_tmp_file(self, name):
        with open(os.path.join(self.TMP_PATH, name), 'r') as f:
            return f.read()
