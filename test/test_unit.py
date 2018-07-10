import unittest
from ctypes import *
from libxray_pybind import Xray
from time import sleep


# TEST
class test_struct(Structure):
    _fields_ = [
        ("a", c_int),
        ("b", c_int),
        ("c", c_int)
    ]


test_array = (test_struct * 10)()

for t in test_array:
    t.a = 1
    t.b = 1
    t.c = 1

_test_data = -1
def _test_on_cb(data):
    global _test_data
    _test_data = 1


def _test_off_cb(data):
    print("setting off")
    global _test_data
    _test_data = 0

class SystemTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.libxray = Xray()
        assert(cls.libxray.init("apikey") == 0)

        assert(cls.libxray.create_type("test_type", sizeof(test_struct)) == 0)
        assert(cls.libxray.add_slot("test_type", "a", 0, 4, "int") == 0)
        assert(cls.libxray.add_slot("test_type", "b", 4, 4, "int") == 0)
        assert(cls.libxray.add_slot("test_type", "c", 8, 4, "int") == 0)


    @classmethod
    def tearDownClass(cls):
        pass

    def setUp(self):
        pass

    def tearDown(self):
        pass


    def test_register(self):
        self.assertEquals(self.libxray.register("test_type", test_array, "/register", 10), 0)
        self.assertEquals(self.libxray.dump("/register"),
                          [[u'a', u'b', u'c'], [u'1', u'1', u'1'], [u'1', u'1', u'1'], [u'1', u'1', u'1'],
                           [u'1', u'1', u'1'], [u'1', u'1', u'1'], [u'1', u'1', u'1'], [u'1', u'1', u'1'],
                           [u'1', u'1', u'1'], [u'1', u'1', u'1'], [u'1', u'1', u'1']])

    def test_push(self):
        t1 = test_struct(1,1,1)
        self.assertEquals(self.libxray.register_push("test_type", "/register_push"), 0)
        self.assertEquals(self.libxray.push("/register_push", t1), 0)
        self.assertEquals(self.libxray.dump("/register_push"),
            [[u'a', u'b', u'c'], [u'1', u'1', u'1']]
        )

        """ check overlap """
        for i in range(2,102):
            t1 = test_struct(i, i, i)
            self.assertEquals(self.libxray.push("/register_push", t1), 0)
        pushed =  self.libxray.dump("/register_push")
        self.assertEquals(pushed[1], [u'2', u'2', u'2'])
        self.assertEquals(pushed[-1], [u'101', u'101', u'101'])

    def test_ondemand_set_cbs(self):
        data = -1
        self.assertEquals(self.libxray.register_push("test_type", "/register_push1"), 0)
        self.assertEquals(self.libxray.xray_set_cb("/register_push1", _test_on_cb, _test_off_cb, data), 0)
        self.libxray.dump("/register_push1")
        self.assertEqual(_test_data, 1)
        sleep(8.1)
        self.assertEqual(_test_data, 0)


if __name__ == '__main__':
    unittest.main()

