import os
import pathlib
import pytest
import shutil
import subprocess
import yaml


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
        substrings = cls.join(cls.to_array_if_str(test_config.get("substrings", [])))
        input = cls.join(cls.to_array_if_str(test_config.get("input", [])))
        output = cls.join(cls.to_array_if_str(test_config.get("output", [])))
        args = cls.to_array_if_str(test_config.get("args", []))
        return cls(name, substrings, input, output, args)

    @staticmethod
    def to_array_if_str(value):
        return [value] if isinstance(value, str) else value
    
    @staticmethod
    def join(lines):
        return "\n".join(lines)


class Test:

    CASES_PATH = "cases"
    TMP_PATH = "tmp"
    BUILD_PATH = os.path.join("..", "build")

    def setup_method(self):
        shutil.rmtree(self.TMP_PATH)
        shutil.copytree(self.BUILD_PATH, self.TMP_PATH)

    @staticmethod
    def get_cases():
        for (dirpath, _, filenames) in os.walk(Test.CASES_PATH):
            for filename in filenames:
                yield CaseModel.parse(os.path.join(dirpath, filename))

    @pytest.mark.parametrize("case", get_cases(), ids=lambda case: case.name)
    def test(self, case):
        self.write_tmp_file("input", case.input)
        self.write_tmp_file("substrings", case.substrings)
        cmd = f"findany {" ".join(case.args)} substrings < input > output"
        subprocess.Popen(cmd, shell=True, cwd=self.TMP_PATH).wait()
        actual = self.read_tmp_file("output")
        assert case.output == actual

    def write_tmp_file(self, name, content):
        with open(os.path.join(self.TMP_PATH, name), 'w') as f:
            f.write(content)

    def read_tmp_file(self, name):
        with open(os.path.join(self.TMP_PATH, name), 'r') as f:
            return f.read()
