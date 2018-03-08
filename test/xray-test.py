import unittest
import os
import json
from subprocess import check_output, PIPE, Popen

DIST_DIR = os.path.join(os.getenv('XRAY_ROOT'), 'dist')

class CannotStartTestApp(Exception):
    pass


class TestApp:

    def _start_testapp(self):
        test_app_path = os.path.join(DIST_DIR, 'test-app')

        print("Starting TestApp %s" % test_app_path)
        self.test_app = Popen(test_app_path,
                              stdout=PIPE,
                              stdin=PIPE,
                              stderr=PIPE)

        # check up
        if_up = self.run_cmd('/is_up')
        if if_up != [['is_up'], ['UP']]:
            raise CannotStartTestApp()
        print("TestApp is up")

    def __init__(self):
        self.test_app = None
        self._start_testapp()

    def run_cmd(self, path):
        result = check_output(['xraycli', '/test-app' + path, '--json'])
        js = json.loads(result)

        return js['result_set']

    def close(self):
        if self.test_app:
            self.test_app.terminate()

            print("\nstdout:")
            print(self.test_app.stdout.readlines())


class TestXray(unittest.TestCase):
    
    @classmethod
    def setUpClass(cls):
        cls.test_app = TestApp()

    @classmethod
    def tearDownClass(cls):
        cls.test_app.close()
     
    def setUp(self):
        pass

    def tearDown(self):
        pass

    def test_basic_types(self):
        basic_types = self.test_app.run_cmd('/basic_types')
        self.assertEquals(basic_types,
                          [[u'int8', u'uint8', u'int16', u'uint16', u'int32', u'uint32', u'int64', u'uint64', u'str',
                            u'p_str'],
                           [u'0', u'0', u'0', u'0', u'0', u'0', u'0', u'0', u'', u'(null)'],
                           [u'-1', u'255', u'-1', u'65535', u'-1', u'4294967295', u'-1', u'18446744073709551615',
                            u'ABCDEFG', u'ABCDEFG']])

    def test_unregister(self):
        all_paths = self.test_app.run_cmd('/')
        self.assertIn(['/bthis/bis/b/ctest'], all_paths)
        
        
        self.assertNotIn(['/this/is/a/test/path'], all_paths)
        self.assertNotIn(['/this/is/a/test'], all_paths)
        self.assertNotIn(['/this/is/a'], all_paths)
        self.assertNotIn(['/this/is'], all_paths)
        self.assertNotIn(['/this'], all_paths)
        
        self.assertNotIn(['/bthis/bis/b/btest/bpath'], all_paths)
        self.assertNotIn(['/bthis/bis/b/btest'], all_paths)
        
if __name__ == '__main__':
    unittest.main()
