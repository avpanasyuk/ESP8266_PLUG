Import("env")
import subprocess


def git_rev():
    try:
        rev = (subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            stderr=subprocess.DEVNULL).decode().strip())
        if subprocess.run(["git", "diff", "--quiet"]).returncode != 0:
            rev += "-dirty"
        return rev
    except Exception:
        return "nogit"


env.Append(CPPDEFINES=[("GIT_REV", env.StringifyMacro(git_rev()))])
