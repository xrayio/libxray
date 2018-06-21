import unittest
from ctypes import *
from libxray_pybind import Xray


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


class SystemTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.libxray = Xray()
        assert(cls.libxray.init("apikey") == 0)

    @classmethod
    def tearDownClass(cls):
        pass

    def setUp(self):
        pass

    def tearDown(self):
        pass

    def test_sanity(self):

        self.assertEquals(self.libxray.create_type(
            "test_type", sizeof(test_struct)), 0)

        self.assertEquals(self.libxray.add_slot(
            "test_type", "a", 0, 4, "int"), 0)

        self.assertEquals(self.libxray.add_slot(
            "test_type", "b", 4, 4, "int"), 0)

        self.assertEquals(self.libxray.add_slot(
            "test_type", "c", 8, 4, "ui_hex_t"), 0)

        self.assertEquals(self.libxray.register(
            "test_type", test_array, "/test", 10), 0)

        self.assertEquals(self.libxray.dump("/test"),
                          [[u'a', u'b', u'c'],
                           [u'1', u'1', u'0x00000001'],
                           [u'1', u'1', u'0x00000001'],
                           [u'1', u'1', u'0x00000001'],
                           [u'1', u'1', u'0x00000001'],
                           [u'1', u'1', u'0x00000001'],
                           [u'1', u'1', u'0x00000001'],
                           [u'1', u'1', u'0x00000001'],
                           [u'1', u'1', u'0x00000001'],
                           [u'1', u'1', u'0x00000001'],
                           [u'1', u'1', u'0x00000001']])


if __name__ == '__main__':
    unittest.main()

