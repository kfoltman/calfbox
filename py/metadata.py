#! /usr/bin/env python3
# -*- coding: utf-8 -*-

"""
This file implements the JackIO Python side of Jack Medata as described here:
    http://www.jackaudio.org/files/docs/html/group__Metadata.html

"""

#get_thing
from _cbox import do_cmd

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
