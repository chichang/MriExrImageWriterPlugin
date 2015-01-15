// ------------------------------------------------------------------
// MriExrImageWriterPlugin.cpp
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

MriImagePluginResult save(MriImageHandle ImageHandle, const char *pFileName, const char **ppMessageStringOut,
                          MriImageDataFormat Format, int NumMipMapLevels, int TileWidth, int TileHeight)
{
    MriImagePluginResult Result;
    MriImageMipMapInfo MipMapInfo;
    int NumChannels, ChannelSize;
    int BufferSize, MipMapLevel, PixcelSize;
    int TileSizeX, TileSizeY, TileSize, TileX, TileY;
    int HashSize, Scanline, buf_y, buf_x;
    int Width, Height;
    unsigned char *pBuffer = NULL;
    unsigned char *pPtr = NULL;
    char *pHash = NULL;
    float pixel[4];
    half *pHalf;
    void *pVoid;
    MipMapLevel = 0;

    //todo:check if the channel is supported.
    //channel format.
    switch (Format)
    {
    case MRI_IDF_BYTE_RGB:
        NumChannels = 3;
        ChannelSize = 1;
        break;
    case MRI_IDF_BYTE_RGBA:
        NumChannels = 4;
        ChannelSize = 1;
        break;
    case MRI_IDF_HALF_RGB:
        NumChannels = 3;
        ChannelSize = 2;
        break;
    //MRI_IDF_HALF_RGBA
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
    //bytes of y row
    TileSizeY = TileHeight * PixcelSize;
    TileSizeX = TileWidth * PixcelSize;
    TileSize = TileWidth * TileSizeY;

    //for this we only care about level 0 since OpenImageIO takes care of the mipmap.
    for (; MipMapLevel  <= 0 ; ++MipMapLevel)
    {
        //getting info of current mipmap level.
        Result = s_Host.getMipMapInfo(ImageHandle, MipMapLevel, &MipMapInfo);
        if (MRI_IPR_SUCCEEDED != Result)
        {
            s_Host.trace("Failed: %d", Result);
            return Result;
        }

        //current level res
        Width = MipMapInfo.m_Width;
        Height = MipMapInfo.m_Height;

        //create oiio objects
        ImageSpec spec (Width, Height, NumChannels, TypeDesc::FLOAT);
        ImageBuf Input("mriExrWriterBuf", spec);

        // Allocate a buffer big enough to hold a complete set of tiles in the x axis.
        BufferSize = MipMapInfo.m_Width * TileSizeY;
        pBuffer = (unsigned char*)malloc(BufferSize);

        //s_Host.trace("Mip-map %d tile count: %dx%d", MipMapLevel, MipMapInfo.m_NumTilesX, MipMapInfo.m_NumTilesY);
        for (TileY = 0; TileY < MipMapInfo.m_NumTilesY; ++TileY)
        {
            //reset every new Y tile.
            pPtr = pBuffer;

            //for each Y row. loop through all x tiles.
            for (TileX = 0; TileX < MipMapInfo.m_NumTilesX; ++TileX)
            {
                Result = s_Host.getTileHashSize(ImageHandle, TileX, TileY, MipMapLevel, &HashSize);
                if (MRI_IPR_SUCCEEDED != Result)
                {
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
                //looping through each x tile...
                //s_Host.trace("Filling in tile (%d, %d) - {%s}\n", TileX, TileY, pHash);
                free(pHash);

                //load the tile bites into the buffer.
                Result = s_Host.loadTile(ImageHandle, TileX, TileY, MipMapLevel, pPtr, TileSize);

                //pData   The address of a buffer that is at least DataSizeBytes long that will receive the image data from the tile
                if (MRI_IPR_SUCCEEDED != Result)
                {
                    s_Host.trace("Failed: %d", Result);
                    free(pBuffer);
                    return Result;
                }
                //move the pointer begining of the next tile. 
                pPtr += TileSize;
            }
            // The whole row of X tiles loaded into buffer.

            // not for the example can just keeps writing. for our frame buffer we need to shift the base
            // for each y row.
            // write each scanline of the Y row into the file
            for (Scanline = 0; Scanline < TileHeight; ++Scanline)
            {
                //set cordinate for image buffer y scanline off set
                buf_y = (TileY*TileHeight)+Scanline;
                buf_x = 0;

                //set the pointer to the begining of each scanline and start writing.
                //push the pointer to the start of next scanline.
                pPtr = pBuffer + (Scanline * TileSizeX);

                //loop through the scanline of each X tiles
                for (TileX = 0; TileX < MipMapInfo.m_NumTilesX; ++TileX)
                {
                    //cast pointer to a half pointer
                    pVoid = pPtr;
                    pHalf = static_cast<half*>(pVoid);

                    //figureout how many bytes are in the tile scanline
                    //loop through the current scanline and grab the value as half
                    //TileSizeX( int bytes)/(sizeof(half)*numChannels)
                    for (int i=0; i < TileSizeX/(2*4); ++ i)
                    {
                        //todo: add in other channal type support.(for now only 16bit RGBA)
                        //for p<NumChannels. set valu for each channel on the pixcel.
                        for (int p=0; p<4; ++p)
                        {
                            pixel[p]=(float)*pHalf;
                            pHalf += 1; 
                        }
                        //set pixcel on image buffer.
                        //todo: use image buffer iterator to get better performance.
                        Input.setpixel(buf_x,buf_y,pixel);
                        buf_x += 1;

                    }
                    //move pPtr to next tile
                    pPtr += TileSize;
                }
            }
        }
        free(pBuffer);

        //config for maketx
        ImageSpec config;
        config.attribute ("maketx:highlightcomp", 1);
        config.attribute ("maketx:filtername", "lanczos3");
        config.attribute ("maketx:opaquedetect", 1);

        //metadata
        config.attribute("show", getenv("SHOW"));
        config.attribute("shot", getenv("SHOT"));
        config.attribute("artist", getenv("USER"));

        //write image
        stringstream s; //s.str()
        bool success = ImageBufAlgo::make_texture (ImageBufAlgo::MakeTxTexture,Input, pFileName, config, &s);
        if (! success)
        {
            s_Host.trace("Fail to write image.");
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
    static MriFileFormatDesc s_Format = { "exr", "mipmap exr writer plug-in" };
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
    s_Plugin.name               = "MrxExrWriter";
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
