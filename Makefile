#-------------------------------------------------------------------------------
# Mari OIIO Mip-Map Exr Image Writer Plugin
#-------------------------------------------------------------------------------
MRI_VERSION = 3.0v2
MRI_INCLUDE_DIR = /X/tools/binlinux/apps/Mari$(MRI_VERSION)/SDK/include

PLUGIN_NAME = CcExrImageWriterPlugin
SRC_NAME = $(PLUGIN_NAME).cpp
LIB_NAME = lib$(PLUGIN_NAME).so
#INSTALL_DIR = build
INSTALL_DIR = /X/tools/mari/mrx/plugins

INSTALL_NAME = $(INSTALL_DIR)/$(LIB_NAME)

EXR_INCLUDE_DIR = /X/tools/packages/gcc-4.1/openexr/openexr_1.7.1/include
EXR_LIB_DIR = /X/tools/packages/gcc-4.1/openexr/openexr_1.7.1/lib

OIIO_INCLUDE_DIR = /X/tools/packages/gcc-4.1/oiio/oiio-1.4.9/include
OIIO_LIB_DIR = /X/tools/packages/gcc-4.1/oiio/oiio-1.4.9/lib

$(LIB_NAME): $(SRC_NAME) $(MRI_INCLUDE_DIR)/FnPluginSystem.h $(MRI_INCLUDE_DIR)/Mari.h $(MRI_INCLUDE_DIR)/MriUserPluginCommon.h $(MRI_INCLUDE_DIR)/MriImagePluginCommon.h $(MRI_INCLUDE_DIR)/MriImageWriterPlugin.h $(EXR_INCLUDE_DIR)/OpenEXR/half.h $(OIIO_INCLUDE_DIR)/OpenImageIO/imageio.h $(OIIO_INCLUDE_DIR)/OpenImageIO/typedesc.h $(OIIO_INCLUDE_DIR)/OpenImageIO/imagebuf.h $(OIIO_INCLUDE_DIR)/OpenImageIO/imagebufalgo.h
	gcc -o $(LIB_NAME) -fPIC -shared -Wall -g -I$(MRI_INCLUDE_DIR) -I$(EXR_INCLUDE_DIR)/OpenEXR -I$(OIIO_INCLUDE_DIR) $(SRC_NAME) -L$(EXR_LIB_DIR) -lHalf -L$(OIIO_LIB_DIR) -lOpenImageIO $(EXR_LIB_DIR)/libIlmThread.a -lOpenImageIO_Util

$(INSTALL_NAME):
	mkdir -p $(INSTALL_DIR)
	#ln -s $(CURDIR)/$(LIB_NAME) $(INSTALL_NAME)
	mv $(LIB_NAME) $(INSTALL_NAME)

clean:
	rm -f $(INSTALL_NAME)

install: build $(INSTALL_NAME)

build: $(LIB_NAME)

rebuild: clean build

all: buildls

