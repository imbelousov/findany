import subprocess
import os
import sys

def main():
    bin_name = "findany.exe" if os.name == "nt" else "findany"
    bin_path = os.path.join(os.path.dirname(__file__), "bin", bin_name)

    if not os.path.isfile(bin_path):
        print(f"Error: binary {bin_path} not found in 'findany/bin/'", file=sys.stderr)
        sys.exit(1)

    subprocess.run([bin_path] + sys.argv[1:])
