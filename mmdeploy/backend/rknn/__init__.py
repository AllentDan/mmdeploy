# Copyright (c) OpenMMLab. All rights reserved.
import importlib
import re
import subprocess


def is_available():
    """Check whether rknn is installed.

    Returns:
        bool: True if rknn package is installed.
    """
    return importlib.util.find_spec('rknn') is not None


def package_info():
    import pkg_resources
    for p in pkg_resources.working_set:
        if p.project_name.startswith('rknn-toolkit'):
            return dict(name=p.project_name, version=p.version)
    return dict(name=None, version=None)


def device_available():
    """Check whether device available.

    Returns:
        bool: True if the device is available.
    """
    ret = subprocess.check_output('adb devices', shell=True)
    match = re.search(r'\\n\w+\\tdevice', str(ret))
    return match is not None


__all__ = []

if is_available():
    from .wrapper import RKNNWrapper
    __all__ += ['RKNNWrapper']
