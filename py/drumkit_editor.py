import cbox
from gui_tools import *

class EditorDialog(gtk.Dialog):
    def __init__(self, parent):
        gtk.Dialog.__init__(self, "Drum kit editor", parent, gtk.DIALOG_MODAL, 
            (gtk.STOCK_OK, gtk.RESPONSE_OK))
        self.set_default_response(gtk.RESPONSE_OK)
        self.vbox.pack_start(gtk.Label("Insert widgets here"))
        self.vbox.show_all()
