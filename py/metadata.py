#! /usr/bin/env python3
# -*- coding: utf-8 -*-

"""
This file implements the JackIO Python side of Jack Medata as described here:
    http://www.jackaudio.org/files/docs/html/group__Metadata.html

"""
import base64 # for icons
import os.path

#get_thing
from calfbox._cbox2 import do_cmd

def get_thing(): pass #overwritten by cbox.py after import

class Metadata:
    @staticmethod
    def get_property(port, key):
        """port is the portname as string System:out_1
        returns tuple (value, mime/type)"""
        result = get_thing("/io/get_property", "/value", [(str, str)], port, key)
        return result

    @staticmethod
    def get_properties(port):
        """returns a list of tuples (key, value, mime/type)"""
        result = get_thing("/io/get_properties", "/properties", [(str, str, str)], port)
        return result

    @staticmethod
    def get_all_properties():
        """returns a list of tuples (portname, key, value, mime/type)"""
        result = get_thing("/io/get_all_properties", "/all_properties", [(str, str, str, str)])
        return result

    @staticmethod
    def set_property(port, key, value, jackPropertyType=""):
        """empty jackPropertyType leads to UTF-8 string
        for convenience we see if value is a python int and send the right jack_property_t::type
        jackio.c checks if the port exists, even though metadata allows keys for non-existent uuids.
        """
        if type(value) is int:
            jackPropertyType = "http://www.w3.org/2001/XMLSchema#int"
            value = str(value)
        elif not type(value) is str:
            return TypeError("value {} must be int or str but was {}".format(value, type(value)))
        do_cmd("/io/set_property", None, [port, key, value, jackPropertyType])

    @staticmethod
    def client_set_property(key, value, jackPropertyType=""):
        """empty jackPropertyType leads to UTF-8 string
        for convenience we see if value is a python int and send the right jack_property_t::type

        This is directly for our client, which we do not need to provide here.
        """
        if type(value) is int:
            jackPropertyType = "http://www.w3.org/2001/XMLSchema#int"
            value = str(value)
        elif not type(value) is str:
            return TypeError("value {} must be int or str but was {}".format(value, type(value)))
        do_cmd("/io/client_set_property", None, [key, value, jackPropertyType])

    @staticmethod
    def remove_property(port, key):
        """port is the portname as string System:out_1"""
        do_cmd("/io/remove_property", None, [port, key])

    @staticmethod
    def remove_properties(port):
        """port is the portname as string System:out_1"""
        do_cmd("/io/remove_properties", None, [port])

    @staticmethod
    def remove_all_properties():
        """Remove all metadata from jack server"""
        do_cmd("/io/remove_all_properties", None, [])


    #Higher Level Functions

    @staticmethod
    def set_port_order(port, index):
       """
       port is the portname as string including a client name System:out_1

       https://github.com/drobilla/jackey

       Order for a port.
       This is used to specify the best order to show ports in user interfaces.
       The value MUST be an integer.  There are no other requirements, so there may
       be gaps in the orders for several ports.  Applications should compare the
       orders of ports to determine their relative order, but must not assign any
       other relevance to order values.

       #define JACKEY_ORDER "http://jackaudio.org/metadata/order"
       """
       Metadata.set_property(port, "http://jackaudio.org/metadata/order", index) #automatically converted to int-mime

    @staticmethod
    def set_all_port_order(pDict):
        """
        pDict portname as string : index as integer
        """
        if not (len(pDict.values()) == len(set(pDict.values()))):
            raise ValueError("All indices for ordering must be unique")

        for port, index in pDict.items():
            Metadata.set_port_order(port, index)

    @staticmethod
    def set_pretty_name(port, name):
        """port is the portname as string including a client name System:out_1
        Name however is just the port name, without a client."""
        Metadata.set_property(port, "http://jackaudio.org/metadata/pretty-name", name)


    @staticmethod
    def set_icon_name(freeDeskopIconName):
        """
        This sets the name of the icon according to freedesktop specs.
        The name is the basename without extension like so:
        /usr/share/icons/hicolor/32x32/apps/patroneo.png -> "patroneo"

        The name of the icon for the subject (typically client).
        This is used for looking up icons on the system, possibly with many sizes or themes. Icons
        should be searched for according to the freedesktop Icon
        Theme Specification:
        https://specifications.freedesktop.org/icon-theme-spec/icon-theme-spec-latest.html
        """
        if not os.path.splitext(freeDeskopIconName)[0] == freeDeskopIconName:
            raise ValueEror(f"Icon name must not have a file extension. Expected {os.path.splitext(freeDeskopIconName)[0]} but was {freeDeskopIconName}")

        if not os.path.basename(freeDeskopIconName) == freeDeskopIconName:
            raise ValueError(f"Icon name must not be path. Expected {os.path.basename(freeDeskopIconName)} but was {freeDeskopIconName}")

        Metadata.client_set_property("http://jackaudio.org/metadata/icon-name", freeDeskopIconName)


    @staticmethod
    def set_icon_small(base64png):
        """ A value with a MIME type of "image/png;base64" that is an encoding of an
        NxN (with 32 < N <= 128) image to be used when displaying a visual representation of that
        client or port.

        The small icon of our JACK client.
        Setting icons to ports seems to be technically possible, but this is not the function
        for port-icons.

        This function checks if the data is actually base64 and a shallow test if the data is PNG.
        """
        testDecode = base64.b64decode(base64png)
        if not base64png.encode("utf-8") == base64.b64encode(testDecode):
            raise ValueError("Provided data must be uft-8 and base64 encoded. But it was not")

        if not "PNG" in repr(testDecode)[:16]:
            raise ValueError("Provided data does not seem to be a PNG image. It is missing the PNG header.")

        Metadata.client_set_property("http://jackaudio.org/metadata/icon-small", base64png, jackPropertyType="image/png;base64")

    @staticmethod
    def set_icon_large(base64png):
        """ A value with a MIME type of "image/png;base64" that is an encoding of an
        NxN (with N <=32) image to be used when displaying a visual representation of that client
        or port.

        The large icon of our JACK client.
        Setting icons to ports seems to be technically possible, but this is not the function
        for port-icons.
        This function checks if the data is actually base64 and a shallow test if the data is PNG.
        """

        testDecode = base64.b64decode(base64png)
        if not base64png.encode("utf-8") == base64.b64encode(testDecode):
            raise ValueError("Provided data must be uft-8 and base64 encoded. But it was not")

        if not "PNG" in repr(testDecode)[:16]:
            raise ValueError("Provided data does not seem to be a PNG image. It is missing the PNG header.")

        Metadata.client_set_property("http://jackaudio.org/metadata/icon-large", base64png, jackPropertyType="image/png;base64")
