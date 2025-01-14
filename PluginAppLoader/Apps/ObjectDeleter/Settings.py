# --------------------------------------------
# --------------- DESCRIPTION ----------------
#
# This file let's you edit the settings of this RoboDK App.
#
# More information about the RoboDK API for Python here:
#     https://robodk.com/doc/en/RoboDK-API.html
#     https://robodk.com/doc/en/PythonAPI/index.html
#
# More information on RoboDK Apps here:
#     https://github.com/RoboDK/Plug-In-Interface/tree/master/PluginAppLoader
#
# --------------------------------------------

from robodk import robolink, roboapps, robodialogs


class Settings(roboapps.AppSettings):
    """Object Deleter's App Settings"""

    def __init__(self, settings_param='Object-Deleter-Settings'):
        super().__init__(settings_param)

        from collections import OrderedDict
        self._FIELDS_UI = OrderedDict()

        #-----------------------------------------------------
        self._FIELDS_UI['ZONE_FRAME'] = "Zone Location"
        self.ZONE_FRAME = [0, ['']]

        self._FIELDS_UI['ZONE_RADIUS'] = "Zone radius"
        self.ZONE_RADIUS = 250.

        self.UpdateFrameList()

    def UpdateFrameList(self, RDK=None):
        if RDK is None:
            RDK = robolink.Robolink()

        frames = [RDK.ActiveStation().Name()] + RDK.ItemList(robolink.ITEM_TYPE_FRAME, True)
        if self.ZONE_FRAME[1][self.ZONE_FRAME[0]] not in frames:
            self.ZONE_FRAME[0] = 0
        self.ZONE_FRAME[1] = frames

    def ShowUI(self, *args, **kwargs):
        self.UpdateFrameList()
        return super().ShowUI(*args, **kwargs)

    def SetDefaults(self):
        super().SetDefaults()
        self.UpdateFrameList()


def runmain():
    """
    Entrypoint of this action when it is executed on its own or interacted with in RoboDK.
    Important: Use the function name 'runmain()' if you want to compile this action.
    """

    if roboapps.Unchecked():
        roboapps.Exit()
    else:
        id = robodialogs.InputDialog("Object Deleter ID", 0)
        if id == False:
            roboapps.Exit()  # User cancel
        S = Settings('Object-Deleter-Settings-' + str(id))
        S.Load()
        S.ShowUI('Object Deleter Settings (' + str(id) + ')')


if __name__ == '__main__':
    runmain()