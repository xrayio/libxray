#! /usr/bin/env python

from distutils.core import setup
from setuptools import find_packages

# pip install -e .                                              # install from source
# python setup.py sdist                                         # create dist
# sudo pip install --no-index --find-links=./dist/ xray         # install dist

setup(
    name='xraycli',
    version='0',
    package_dir={'':'cli'},
    packages=find_packages(where="./cli/"),
    install_requires=[
        'nanomsg',
        'tabulate',
        'pandas',
    ],
    license='MIT',
    scripts=['cli/xraycli']
)
