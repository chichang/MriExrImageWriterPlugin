// ------------------------------------------------------------------
// CcExrImageWriterPlugin.cpp
// this file is heavily based on MriExampleImageWriterPlugin.c
// Copyright (c) 2014, Chi-Chang Chu
// www.chichangchu.com
// ------------------------------------------------------------------

#ifdef __cplusplus
extern "C"
{
#endif

#include "MriImageWriterPlugin.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    // Windows DLL export definition for the getPlugins() function
    #define DLLEXPORT __declspec(dllexport)
#else
    //A Linux placeholder library export definition to match Windows
    #define DLLEXPORT
#endif

//------------------------------------------------------------------------------
// Function prototypes and data
//------------------------------------------------------------------------------

// Saves an image file
MriImagePluginResult save(MriImageHandle ImageHandle, const char *pFileName, const char **ppMessageStringOut,
                          MriImageDataFormat Format, int NumMipMapLevels, int TileWidth, int TileHeight);
// Returns the image data formats supported by the plug-in
MriImageDataFormat *supportedImageFormats(const char *pExtension, int *pNumFormatsOut);
// Returns the file formats supported by the plug-in
MriFileFormatDesc *supportedFormats(int *pNumFormatsOut);

// Returns the list of plug-ins in this library
DLLEXPORT FnPlugin *getPlugins(unsigned int *pNumPlugins);
// Sets the host information for a plug-in
FnPluginStatus setImageWriterHost(const FnPluginHost *pHost);
// Returns the suite of functions provided by a plug-in
const void *getImageWriterSuite();
// Cleans up the plug-in
void flushImageWriterSuite();

//The host structure, which contains functions that the plug-in can call
MriImageWriterHostV1 s_Host;

// The plug-in structure, to provide to the host so it can call functions when needed
MriImageWriterPluginV1 s_PluginSuite =
{
    &save,
    &supportedImageFormats,
    &supportedFormats
};

#ifdef __cplusplus
}
#endif

#include <fstream>
using namespace std;

//OpenExr include
#include <half.h>

//OpenImageIO include
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/typedesc.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
OIIO_NAMESPACE_USING

//------------------------------------------------------------------------------
// Plug-in suite function definitions
//------------------------------------------------------------------------------

MriImagePluginResult save(MriImageHandle ImageHandle, 
                            const char *pFileName, 
                            const char **ppMessageStringOut,
                            MriImageDataFormat Format, 
                            int NumMipMapLevels, 
                            int TileWidth, 
                            int TileHeight)
{

    //trace to make sure we are inside the function
    s_Host.trace("saving starts ...");
    MriImagePluginResult Result;

    MriImageMipMapInfo MipMapInfo;

    int NumChannels, ChannelSize;
    int Width, Height;
    int BufferSize, PixcelSize;
    int TileSizeX, TileSizeY, TileSize, TileX, TileY;
    int HashSize;
    int MipMapLevel = 0;

    unsigned char *pBuffer = NULL;
    unsigned char *pPtr = NULL;
    char *pHash = NULL;
    half *pHalf;
    float *pFloat;
    void *pVoid;

    ImageSpec spec;

    ////////////////////////////////////////////////////////////////////////////////////////////////

    //channel format. not supporting 8bit data type.
    switch (Format)
    {
        case MRI_IDF_BYTE_RGB:
            NumChannels = 3;
            ChannelSize = 1;
            s_Host.trace("Channel format not supported: %d", Format);
            return MRI_IPR_FAILED;

        case MRI_IDF_BYTE_RGBA:
            NumChannels = 4;
            ChannelSize = 1;
            s_Host.trace("Channel format not supported: %d", Format);
            return MRI_IPR_FAILED;

        case MRI_IDF_HALF_RGB:
            NumChannels = 3;
            ChannelSize = 2;
            break;

        case MRI_IDF_HALF_RGBA:
            NumChannels = 4;
            ChannelSize = 2;
            break;

        case MRI_IDF_FLOAT_RGB:
            NumChannels = 3;
            ChannelSize = 4;
            break;

        case MRI_IDF_FLOAT_RGBA:
            NumChannels = 4;
            ChannelSize = 4;
            break;

        default:
            s_Host.trace("Invalid format: %d", Format);
            return MRI_IPR_FAILED;
    }

    //bytes of one pixcel.
    PixcelSize = NumChannels * ChannelSize;

    //bytes of one tile.
    TileSizeY = TileHeight * PixcelSize;
    TileSizeX = TileWidth * PixcelSize;
    TileSize = TileWidth * TileSizeY;

    //for this we only care about level 0 since OpenImageIO takes care of the mipmaps.
    for (; MipMapLevel  <= 0 ; ++MipMapLevel) 
    {
        //getting info of current mipmap level.
        s_Host.trace("loading MipMap level: %d", MipMapLevel);
        Result = s_Host.getMipMapInfo(ImageHandle, MipMapLevel, &MipMapInfo);

        if (MRI_IPR_SUCCEEDED != Result)
        {
            s_Host.trace("Failed: %d", Result);
            return Result;
        }

        //current level res
        Width = MipMapInfo.m_Width;
        Height = MipMapInfo.m_Height;

        //oiio spec
        spec.width = Width;
        spec.height = Height;
        spec.nchannels = NumChannels;

        //format
        if (ChannelSize == 2)
        {
            spec.format = TypeDesc::HALF;
        }
        else if (ChannelSize == 4)
        {
            spec.format = TypeDesc::FLOAT;
        } 
        else {}

        ImageBuf Input("mriExrWriterBuf", spec);

        // Allocate a buffer big enough to hold a complete set of tiles in the x axis.
        //BufferSize = MipMapInfo.m_Width * TileSizeY;
        // Allocate a buffer big enough to hold a complete tile.
        BufferSize = (TileSizeX * TileSizeY)*(NumChannels*ChannelSize);

        //Allocates a block of size bytes of memory, returning a pointer to the beginning of the block. (in bytes)
	s_Host.trace("allocating memory of size: %d", BufferSize);
        pBuffer = (unsigned char*)malloc(BufferSize);	//TODO: is this size right? double check...

        //roi
        int t_w = TileWidth;
        int t_h = TileHeight;
        ROI region = get_roi(spec);

        int roi_ybegin = 0;

        s_Host.trace("Mip-map %d tile count: %dx%d", MipMapLevel, MipMapInfo.m_NumTilesX, MipMapInfo.m_NumTilesY);

        // for y
        for (TileY = 0; TileY < MipMapInfo.m_NumTilesY; ++TileY) 
        {
            int roi_xbegin = 0;

            //for each Y row. loop through each x tiles.
            for (TileX = 0; TileX < MipMapInfo.m_NumTilesX; ++TileX) 
            {
                //reset pointer every new tile.
            	pPtr = pBuffer;

                Result = s_Host.getTileHashSize(ImageHandle, TileX, TileY, MipMapLevel, &HashSize);
                if (MRI_IPR_SUCCEEDED != Result) {
                    s_Host.trace("Failed: %d", Result);
                    free(pBuffer);
                    return Result;
                }

                pHash = (char*)malloc(HashSize);
                Result = s_Host.getTileHash(ImageHandle, TileX, TileY, MipMapLevel, pHash, HashSize);

                if (MRI_IPR_SUCCEEDED != Result) 
                {
                    s_Host.trace("Failed: %d", Result);
                    free(pHash);
                    free(pBuffer);
                    return Result;
                }
                free(pHash);

                //s_Host.trace("Filling in tile (%d, %d) - {%s}\n", TileX, TileY, pHash);

                //load the tile bites into the buffer.
                //pData   The address of a buffer that is at least DataSizeBytes long that will receive the image data from the tile
                Result = s_Host.loadTile(ImageHandle, TileX, TileY, MipMapLevel, pPtr, TileSize);

                if (MRI_IPR_SUCCEEDED != Result)
                {
                    s_Host.trace("Failed: %d", Result);
                    free(pBuffer);
                    return Result;
                }

                //one tile loaded
                //set buffer regin to current tile.
                region.xbegin = roi_xbegin;
                region.ybegin = roi_ybegin;
                region.xend = (roi_xbegin+t_w);
                region.yend = (roi_ybegin+t_h);

                //write the loaded tile to image buffer.
                //cast pointer to a half pointer if data type is half.
                pVoid = pPtr;
                pHalf = static_cast<half*>(pVoid);
                pFloat = static_cast<float*>(pVoid);

                //half
                if (ChannelSize == 2)
                {
                    //create iterator for the tile and write it to the buffer
                    for (ImageBuf::Iterator<half> it (Input, region); ! it.done(); ++it) 
                    {
                        if (! it.exists()) // Make sure the iterator is pointing to a pixel in the data window
                        {
                            s_Host.trace("iterator not pointing to a pixel in the data window");
                            continue;
                        } else {}

                        for (int c = region.chbegin; c < region.chend; ++c) 
                        {
                            //it[c] = 0.0f; // clear the value
                            it[c] = (float)*pHalf; //set val on channel.
                            pHalf += 1;
                        }
                    }
                }
                //float
                else if (ChannelSize == 4)
                {
                    for (ImageBuf::Iterator<float> it (Input, region); ! it.done(); ++it)
                    {
                        if (! it.exists()) 
                        {
                            s_Host.trace("iterator not pointing to a pixel in the data window");
                            continue;
                        } else {}

                        for (int c = region.chbegin; c < region.chend; ++c) 
                        {
                            it[c] = *pFloat;
                            pFloat += 1;
                        }
                    }
                } else{}

            //x done
            roi_xbegin += t_w;
            }

        //y done
        roi_ybegin += t_h;
        }

        free(pBuffer);

	s_Host.trace("texture loaded into ImageBuf. ready to write out texture.");

        //config for maketx. TODO: can we get these settings from ui at export time?
        ImageSpec config;
        config.attribute ("maketx:highlightcomp", 1);
        config.attribute ("maketx:compression", "zip");     //one of: "none", "rle", "zip", "piz", "pxr24",
                                                            ///"b44", or "b44a". If the writer receives a request
                                                            //for a compression type it does not recognize, it
                                                            ///will use "zip" by default.
        config.attribute ("maketx:filtername", "lanczos3");
        config.attribute ("maketx:opaquedetect", 1);

        //metadata
        //config.attribute("show", getenv("SEE IF THIS FAILS"));
        //config.attribute("show", getenv("SHOW"));
        //config.attribute("shot", getenv("SHOT"));
        //config.attribute("artist", getenv("USER"));
        config.attribute("mri_writer", "ccMriExrImageWriterPlugin");

        //write out the texture
        stringstream s; //s.str()
        bool success = ImageBufAlgo::make_texture (ImageBufAlgo::MakeTxTexture, Input, pFileName, config, &s);
        if (! success)
        {
            s_Host.trace("Fail to write image. ImageBufAlgo::make_texture");
            return MRI_IPR_FAILED;
        }

    }
    return MRI_IPR_SUCCEEDED;
}

//------------------------------------------------------------------------------
MriImageDataFormat *supportedImageFormats(const char *pExtension, int *pNumFormatsOut)
{
    return NULL;
}

//------------------------------------------------------------------------------
MriFileFormatDesc *supportedFormats(int *pNumFormatsOut)
{
    static MriFileFormatDesc s_Format = { "exr", "mipmap exr writer plug-in by chi-chang chu" };
    *pNumFormatsOut = 1;
    return &s_Format;
}

//------------------------------------------------------------------------------
// Plug-in interface functions
//------------------------------------------------------------------------------

const void *getImageWriterSuite()
{
    return &s_PluginSuite;
}

//------------------------------------------------------------------------------
void flushImageWriterSuite()
{
}

//------------------------------------------------------------------------------
FnPluginStatus setImageWriterHost(const FnPluginHost *pHost)
{
    const void *pHostSuite = NULL;

    if (NULL == pHost)
        return FnPluginStatusError;

    pHostSuite = pHost->getSuite(MRI_IMAGE_WRITER_API_NAME, MRI_IMAGE_WRITER_API_VERSION);
    if (NULL == pHostSuite)
        return FnPluginStatusError;

    s_Host = *(const MriImageWriterHostV1 *)pHostSuite;
    s_Host.trace("Connected to host '%s' version '%s' (%u)", pHost->name, pHost->versionStr, pHost->versionInt);
    return FnPluginStatusOK;
}

//------------------------------------------------------------------------------
DLLEXPORT FnPlugin *getPlugins(unsigned int *pNumPlugins)
{
    static FnPlugin s_Plugin;
    s_Plugin.name               = "CcExrImageWriterPlugin";
    s_Plugin.pluginVersionMajor = 1;
    s_Plugin.pluginVersionMinor = 0;
    s_Plugin.apiName            = MRI_IMAGE_WRITER_API_NAME;
    s_Plugin.apiVersion         = MRI_IMAGE_WRITER_API_VERSION;
    s_Plugin.setHost            = &setImageWriterHost;
    s_Plugin.getSuite           = &getImageWriterSuite;
    s_Plugin.flush              = &flushImageWriterSuite;

    *pNumPlugins = 1;
    return &s_Plugin;
}


/*
default export:
Debug : [          MriImageExporter.cpp:768 ] : Export path: /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/diffuse/v002
Debug : [          MriImageExporter.cpp:769 ] : Export template: WHITE_HOUSE_col_linh_$UDIM.exr
Debug : [          MriOpenExrSource.cpp:815 ] : [ !! ] '/X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/diffuse/v002/WHITE_HOUSE_col_linh_$UDIM.exr' is not a valid OpenEXR file
Debug : [     MriSnChannelInputNode.cpp:238 ] : [ -- ] Refreshing properties for paint node 'Paint 12'
Debug : [          MriOpenExrSource.cpp:815 ] : [ !! ] '/X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/diffuse/v002/WHITE_HOUSE_col_linh_$UDIM.exr' is not a valid OpenEXR file
Debug : [     MriSnChannelInputNode.cpp:238 ] : [ -- ] Refreshing properties for paint node 'Paint 12'
Debug : [          MriOpenExrSource.cpp:815 ] : [ !! ] '/X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/diffuse/v002/WHITE_HOUSE_col_linh_$UDIM.exr' is not a valid OpenEXR file
Debug : [          MriOpenExrSource.cpp:815 ] : [ !! ] '/X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/diffuse/v002/WHITE_HOUSE_col_linh_$UDIM.exr' is not a valid OpenEXR file
Repeat : Message repeating...
Repeat : Last message was repeated 2 more times
Debug : [          MriOpenExrSource.cpp:815 ] : [ !! ] '/X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE_col_linh_$UDIM.exr' is not a valid OpenEXR file
Debug : [          MriOpenExrSource.cpp:815 ] : [ !! ] '/X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/WHITE_HOUSE_col_linh_$UDIM.exr' is not a valid OpenEXR file
Debug : [          MriOpenExrSource.cpp:815 ] : [ !! ] '/X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh_$UDIM.exr' is not a valid OpenEXR file
Debug : [   MriExportSequenceWidget.cpp:275 ] : About to export : /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh_$UDIM.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh_1001.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh_1002.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh_1003.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh_1004.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh_1005.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh_1006.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh_1007.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh_1008.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh_1009.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh_1010.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh_1011.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh_1012.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh_1013.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh_1014.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh_1015.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh_1016.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh_1017.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh_1018.exr
Debug : [           MriImageManager.cpp:1175] : [ 1 ] Found support for image write format exr in MriOpenExrSink@Wetafx.co.nz
Debug : [            MriUndoManager.cpp:275 ] : [UndoManager] End Macro #1 : Export Images
Debug : [            MriUndoManager.cpp:178 ] : [UndoManager] Undo : Export Images
Debug : [            MriUndoManager.cpp:158 ] : [UndoManager] Begin Macro #1 : 
Debug : [            MriUndoManager.cpp:275 ] : [UndoManager] End Macro #1 : 
Debug : [            MriUndoManager.cpp:178 ] : [UndoManager] Undo : 
Debug : [          MriImageExporter.cpp:1999] : Export 22719 ms
Debug : [     MriBackgroundJobManager.h:187 ] : Job 1 took 85169 ms


valid OpenEXR file
Debug : [          MriOpenExrSource.cpp:815 ] : [ !! ] '/X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh_$UDIM.exr' is not a valid OpenEXR file
Repeat : Message repeating...
Repeat : Last message was repeated 2 more times
Debug : [          MriOpenExrSource.cpp:815 ] : [ !! ] '/X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_$UDIM.exr' is not a valid OpenEXR file
Debug : [   MriExportSequenceWidget.cpp:275 ] : About to export : /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_$UDIM.exr
Debug : [           MriImageManager.cpp:1504] : [ 1 ] 'TheFoundry.MriPsdSink' => 'psb', 'psd'
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1001.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1002.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1003.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1004.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1005.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1006.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1007.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1008.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1009.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1010.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1011.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1012.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1013.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1014.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1015.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1016.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1017.exr
Debug : [          MriImageExporter.cpp:1326] : Checking image layer list with 1 layers to export to /X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1018.exr
Debug : [           MriImageManager.cpp:1175] : [ 1 ] Found support for image write format exr in TheFoundry.MriImagePluginSink
Debug : [        MriImagePluginSink.cpp:652 ] : Saving '/X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1017.exr' using plug-in 'CcExrImageWriterPlugin'
Debug : [        MriImagePluginSink.cpp:652 ] : Saving '/X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1018.exr' using plug-in 'CcExrImageWriterPlugin'
Debug : [        MriImagePluginSink.cpp:652 ] : Saving '/X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1016.exr' using plug-in 'CcExrImageWriterPlugin'
Debug : [        MriImagePluginSink.cpp:652 ] : Saving '/X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1001.exr' using plug-in 'CcExrImageWriterPlugin'
Debug : [        MriImagePluginSink.cpp:652 ] : Saving '/X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1013.exr' using plug-in 'CcExrImageWriterPlugin'
Debug : [        MriImagePluginSink.cpp:652 ] : Saving '/X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1004.exr' using plug-in 'CcExrImageWriterPlugin'
Debug : [        MriImagePluginSink.cpp:652 ] : Saving '/X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1014.exr' using plug-in 'CcExrImageWriterPlugin'
Debug : [        MriImagePluginSink.cpp:652 ] : Saving '/X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1006.exr' using plug-in 'CcExrImageWriterPlugin'
Debug : [        MriImagePluginSink.cpp:652 ] : Saving '/X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1003.exr' using plug-in 'CcExrImageWriterPlugin'
Debug : [        MriImagePluginSink.cpp:652 ] : Saving '/X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1012.exr' using plug-in 'CcExrImageWriterPlugin'
Debug : [        MriImagePluginSink.cpp:652 ] : Saving '/X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1011.exr' using plug-in 'CcExrImageWriterPlugin'
Debug : [        MriImagePluginSink.cpp:652 ] : Saving '/X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1008.exr' using plug-in 'CcExrImageWriterPlugin'
Debug : [        MriImagePluginSink.cpp:652 ] : Saving '/X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1015.exr' using plug-in 'CcExrImageWriterPlugin'
Debug : [        MriImagePluginSink.cpp:652 ] : Saving '/X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1007.exr' using plug-in 'CcExrImageWriterPlugin'
Debug : [        MriImagePluginSink.cpp:652 ] : Saving '/X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1005.exr' using plug-in 'CcExrImageWriterPlugin'
Debug : [        MriImagePluginSink.cpp:652 ] : Saving '/X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1009.exr' using plug-in 'CcExrImageWriterPlugin'
Debug : [        MriImagePluginSink.cpp:652 ] : Saving '/X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1002.exr' using plug-in 'CcExrImageWriterPlugin'
Debug : [        MriImagePluginSink.cpp:652 ] : Saving '/X/projects/mena/SHOTS/WHITE_HOUSE/chichang/textures/WHITE_HOUSE/test/WHITE_HOUSE_col_linh2_1010.exr' using plug-in 'CcExrImageWriterPlugin'
Debug : [            MriUndoManager.cpp:275 ] : [UndoManager] End Macro #1 : Export Images
Debug : [            MriUndoManager.cpp:178 ] : [UndoManager] Undo : Export Images
Debug : [            MriUndoManager.cpp:158 ] : [UndoManager] Begin Macro #1 : 
Debug : [            MriUndoManager.cpp:275 ] : [UndoManager] End Macro #1 : 
Debug : [            MriUndoManager.cpp:178 ] : [UndoManager] Undo : 
Debug : [          MriImageExporter.cpp:1999] : Export 14368 ms
Debug : [            MriUndoManager.cpp:328 ] : [UndoManager] Add : Pan
Debug : [     MriBackgroundJobManager.h:187 ] : Job 1 took 200109 ms
*/
