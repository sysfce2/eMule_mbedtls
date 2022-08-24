#!/usr/bin/env python3
"""Common test generation classes and main function.

These are used both by generate_psa_tests.py and generate_bignum_tests.py.
"""

# Copyright The Mbed TLS Contributors
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may
# not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import argparse
import os
import posixpath
import re
from typing import Callable, Dict, Iterable, List, Type, TypeVar

from mbedtls_dev import test_case

T = TypeVar('T') #pylint: disable=invalid-name


class BaseTarget:
    """Base target for test case generation.

    Attributes:
        count: Counter for test class.
        desc: Short description of test case.
        func: Function which the class generates tests for.
        gen_file: File to write generated tests to.
        title: Description of the test function/purpose.
    """
    count = 0
    desc = ""
    func = ""
    gen_file = ""
    title = ""

    def __init__(self) -> None:
        type(self).count += 1

    @property
    def args(self) -> List[str]:
        """Create list of arguments for test case."""
        return []

    @property
    def description(self) -> str:
        """Create a numbered test description."""
        return "{} #{} {}".format(self.title, self.count, self.desc)

    def create_test_case(self) -> test_case.TestCase:
        """Generate test case from the current object."""
        tc = test_case.TestCase()
        tc.set_description(self.description)
        tc.set_function(self.func)
        tc.set_arguments(self.args)

        return tc

    @classmethod
    def generate_tests(cls):
        """Generate test cases for the target subclasses."""
        for subclass in sorted(cls.__subclasses__(), key=lambda c: c.__name__):
            yield from subclass.generate_tests()


class TestGenerator:
    """Generate test data."""
    def __init__(self, options) -> None:
        self.test_suite_directory = self.get_option(options, 'directory',
                                                    'tests/suites')

    @staticmethod
    def get_option(options, name: str, default: T) -> T:
        value = getattr(options, name, None)
        return default if value is None else value

    def filename_for(self, basename: str) -> str:
        """The location of the data file with the specified base name."""
        return posixpath.join(self.test_suite_directory, basename + '.data')

    def write_test_data_file(self, basename: str,
                             test_cases: Iterable[test_case.TestCase]) -> None:
        """Write the test cases to a .data file.

        The output file is ``basename + '.data'`` in the test suite directory.
        """
        filename = self.filename_for(basename)
        test_case.write_data_file(filename, test_cases)

    # Note that targets whose names contain 'test_format' have their content
    # validated by `abi_check.py`.
    TARGETS = {} # type: Dict[str, Callable[..., test_case.TestCase]]

    def generate_target(self, name: str, *target_args) -> None:
        """Generate cases and write to data file for a target.

        For target callables which require arguments, override this function
        and pass these arguments using super() (see PSATestGenerator).
        """
        test_cases = self.TARGETS[name](*target_args)
        self.write_test_data_file(name, test_cases)

def main(args, generator_class: Type[TestGenerator] = TestGenerator):
    """Command line entry point."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--list', action='store_true',
                        help='List available targets and exit')
    parser.add_argument('targets', nargs='*', metavar='TARGET',
                        help='Target file to generate (default: all; "-": none)')
    options = parser.parse_args(args)
    generator = generator_class(options)
    if options.list:
        for name in sorted(generator.TARGETS):
            print(generator.filename_for(name))
        return
    if options.targets:
        # Allow "-" as a special case so you can run
        # ``generate_xxx_tests.py - $targets`` and it works uniformly whether
        # ``$targets`` is empty or not.
        options.targets = [os.path.basename(re.sub(r'\.data\Z', r'', target))
                           for target in options.targets
                           if target != '-']
    else:
        options.targets = sorted(generator.TARGETS)
    for target in options.targets:
        generator.generate_target(target)
