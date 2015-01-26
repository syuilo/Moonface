#include "Moonface.h"
#include <stdlib.h>
#include <math.h>

#include "MT.h"


static PF_Err About(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
    AEGP_SuiteHandler suites(in_data->pica_basicP);

    suites.ANSICallbacksSuite1()->sprintf(out_data->return_msg,
        "%s v%d.%d\r%s",
        STR(StrID_Name),
        MAJOR_VERSION,
        MINOR_VERSION,
        STR(StrID_Description));

    ShellExecute(NULL, "open", "http://syuilo.com", NULL, NULL, SW_SHOW);

    return PF_Err_NONE;
}

static PF_Err GlobalSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef	 *params[], PF_LayerDef *output)
{
    out_data->my_version = PF_VERSION(MAJOR_VERSION,
        MINOR_VERSION,
        BUG_VERSION,
        STAGE_VERSION,
        BUILD_VERSION);

    out_data->out_flags = PF_OutFlag_DEEP_COLOR_AWARE |	// just 16bpc, not 32bpc
        PF_OutFlag_I_EXPAND_BUFFER |
        PF_OutFlag_I_HAVE_EXTERNAL_DEPENDENCIES;

    out_data->out_flags2 = PF_OutFlag2_SUPPORTS_QUERY_DYNAMIC_FLAGS |
        PF_OutFlag2_I_USE_3D_CAMERA |
        PF_OutFlag2_I_USE_3D_LIGHTS;

    return PF_Err_NONE;
}

static PF_Err ParamsSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef	 *params[], PF_LayerDef *output)
{
    PF_Err		err = PF_Err_NONE;
    PF_ParamDef	def;

    // ------------------------
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Block size",
        1,
        2048,
        1,
        1024,
        64,
        1);

    // ------------------------
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Vibration",
        1,
        8192,
        1,
        1024,
        64,
        2);

    // ------------------------
    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Direction",
        3);

    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOX("",
        "Top",
        TRUE,
        0,
        4);

    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOX("",
        "Bottom",
        TRUE,
        0,
        5);

    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOX("",
        "Left",
        TRUE,
        0,
        6);

    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOX("",
        "Right",
        TRUE,
        0,
        7);

    // ------------------------
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(8);

    // ------------------------
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("PRNG",
        2,
        2,
        "線形合同法|メルセンヌ・ツイスタ (MT19937)",
        9);

    // ------------------------
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Random seed",
        0,
        16384,
        0,
        16384,
        0,
        10);

    // ------------------------
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Border",
        4,
        2,
        "None|Expand|Mirror|Tile",
        11);

    // ------------------------
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOX("Merge",
        "元画像と合成します。",
        TRUE,
        0,
        12);

    out_data->num_params = MOONFACE_NUM_PARAMS;

    return err;
}

static PF_Err pSet(PF_Pixel *data, A_long x, A_long y, A_long width, PF_Pixel pixel)
{
    data[x + y * width] = pixel;
    return PF_Err_NONE;
}

static PF_Pixel pGet(PF_Pixel *data, A_long x, A_long y, A_long width)
{
    return data[x + y * width];
}

static PF_Err Render(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
    PF_Err				err = PF_Err_NONE;
    AEGP_SuiteHandler	suites(in_data->pica_basicP);

    PF_EffectWorld *input = &params[MOONFACE_INPUT]->u.ld;
    PF_Boolean is16Bit PF_WORLD_IS_DEEP(output);

    ParamInfo niP;
    AEFX_CLR_STRUCT(niP);

    niP.size = params[MOONFACE_SIZE]->u.sd.value;
    niP.vibration = params[MOONFACE_VIBRATION]->u.sd.value;
    niP.directionTop = params[MOONFACE_DIRECTION_TOP]->u.bd.value;
    niP.directionBottom = params[MOONFACE_DIRECTION_BOTTOM]->u.bd.value;
    niP.directionLeft = params[MOONFACE_DIRECTION_LEFT]->u.bd.value;
    niP.directionRight = params[MOONFACE_DIRECTION_RIGHT]->u.bd.value;
    niP.prng = params[MOONFACE_PRNG]->u.pd.value;
    niP.seed = params[MOONFACE_SEED]->u.sd.value;
    niP.border = params[MOONFACE_BORDER]->u.pd.value;
    niP.merge = params[MOONFACE_MERGE]->u.bd.value;

    A_long width = input->width;
    A_long height = input->height;

    A_long outWidth, inWidth;
    if (is16Bit == TRUE) {
        outWidth = output->rowbytes / sizeof(PF_Pixel16);
        inWidth = input->rowbytes / sizeof(PF_Pixel16);
    } else {
        outWidth = output->rowbytes / sizeof(PF_Pixel);
        inWidth = input->rowbytes / sizeof(PF_Pixel);
    }

    if (niP.merge)
        PF_COPY(input, output, NULL, NULL);

    // タイル数を算出
    int xtile = (int)(width / niP.size) + 1;
    int ytile = (int)(height / niP.size) + 1;

    switch (niP.prng)
    {
        case 1:
            srand(niP.seed);
            break;
        case 2:
            init_genrand(niP.seed);
            break;
    }

    for (int ix = -1; ix < xtile; ix++) {
        for (int iy = -1; iy < ytile; iy++)
        {
            // 揺れを算出
            int xr = 0;
            int yr = 0;

            switch (niP.prng)
            {
                case 1: // 線形合同法
                    if (niP.directionTop)
                        yr += -(rand() % niP.vibration);
                    if (niP.directionBottom)
                        yr += (rand() % niP.vibration);
                    if (niP.directionLeft)
                        xr += -(rand() % niP.vibration);
                    if (niP.directionRight)
                        xr += (rand() % niP.vibration);
                    break;
                case 2: // MT
                    if (niP.directionTop)
                        yr += -(genrand_int32() % niP.vibration);
                    if (niP.directionBottom)
                        yr += (genrand_int32() % niP.vibration);
                    if (niP.directionLeft)
                        xr += -(genrand_int32() % niP.vibration);
                    if (niP.directionRight)
                        xr += (genrand_int32() % niP.vibration);
                    break;
            }

            // 1タイルの描画
            for (int tx = 0; tx < niP.size; tx++) {
                for (int ty = 0; ty < niP.size; ty++)
                {
                    int px = (ix * niP.size) + tx;
                    int py = (iy * niP.size) + ty;

                    int setx = (px + xr);
                    int sety = (py + yr);

                    // 1タイルの1ピクセルの描画
                    if ((setx >= 0) && (setx < width) && (sety >= 0) && (sety < height))
                    {
                        // 16bit
                        if (is16Bit == TRUE) {
                            PF_Pixel16 pixel;

                            PF_Pixel16 *outData = (PF_Pixel16 *)output->data;
                            PF_Pixel16 *inData = (PF_Pixel16 *)input->data;

                            //switch (niP.border)
                            //{
                            //    case 1: // None
                            //        if ((px >= 0) && (px < width) && (py >= 0) && (py < height)) // 通常
                            //            pixel = inData[px + py * inWidth];
                            //        else if ((px >= 0) && (px >= width) && (py >= 0) && (py < height)) // → 右はみ出し
                            //            pixel.alpha = 0;
                            //        else if ((px >= 0) && (px < width) && (py >= 0) && (py >= height)) // ↓ 下はみ出し
                            //            pixel.alpha = 0;
                            //        else if ((px >= 0) && (px >= width) && (py >= 0) && (py >= height)) // ↘ 右下はみ出し
                            //            pixel.alpha = 0;
                            //        else if ((px < 0) && (px < width) && (py >= 0) && (py < height)) // ← 左はみ出し
                            //            pixel.alpha = 0;
                            //        else if ((px >= 0) && (px < width) && (py < 0) && (py < height)) // ↑ 上はみ出し
                            //            pixel.alpha = 0;
                            //        else if ((px < 0) && (px < width) && (py < 0) && (py < height)) // ↖ 左上はみ出し
                            //            pixel.alpha = 0;
                            //        break;
                            //    case 2: // Expand
                            //        if ((px >= 0) && (px < width) && (py >= 0) && (py < height)) // 通常
                            //            pixel = inData[px + py * inWidth];
                            //        else if ((px >= 0) && (px >= width) && (py >= 0) && (py < height)) // → 右はみ出し
                            //            pixel = inData[(width - 1) + py * inWidth];
                            //        else if ((px >= 0) && (px < width) && (py >= 0) && (py >= height)) // ↓ 下はみ出し
                            //            pixel = inData[px + (height - 1) * inWidth];
                            //        else if ((px >= 0) && (px >= width) && (py >= 0) && (py >= height)) // ↘ 右下はみ出し
                            //            pixel = inData[(width - 1) + (height - 1) * inWidth];
                            //        else if ((px < 0) && (px < width) && (py >= 0) && (py < height)) // ← 左はみ出し
                            //            pixel = inData[0 + py * inWidth];
                            //        else if ((px >= 0) && (px < width) && (py < 0) && (py < height)) // ↑ 上はみ出し
                            //            pixel = inData[px + 0 * inWidth];
                            //        else if ((px < 0) && (px < width) && (py < 0) && (py < height)) // ↖ 左上はみ出し
                            //            pixel = inData[0 + 0 * inWidth];
                            //        break;
                            //    case 3: // Mirror
                            //        if ((px >= 0) && (px < width) && (py >= 0) && (py < height)) // 通常
                            //            pixel = inData[px + py * inWidth];
                            //        else if ((px >= 0) && (px >= width) && (py >= 0) && (py < height)) // → 右はみ出し
                            //            pixel = inData[(width - (px - width) - 1) + py * inWidth];
                            //        else if ((px >= 0) && (px < width) && (py >= 0) && (py >= height)) // ↓ 下はみ出し
                            //            pixel = inData[px + (height - (py - height) - 1) * inWidth];
                            //        else if ((px >= 0) && (px >= width) && (py >= 0) && (py >= height)) // ↘ 右下はみ出し
                            //            pixel = inData[(width - (px - width) - 1) + (height - (py - height) - 1) * inWidth];
                            //        else if ((px < 0) && (px < width) && (py >= 0) && (py < height)) // ← 左はみ出し
                            //            pixel = inData[x + py * inWidth];
                            //        else if ((px >= 0) && (px < width) && (py < 0) && (py < height)) // ↑ 上はみ出し
                            //            pixel = inData[px + y * inWidth];
                            //        else if ((px < 0) && (px < width) && (py < 0) && (py < height)) // ↖ 左上はみ出し
                            //            pixel = inData[x + y * inWidth];
                            //        break;
                            //    case 4: // Tile
                            //        if ((px >= 0) && (px < width) && (py >= 0) && (py < height)) // 通常
                            //            pixel = inData[px + py * inWidth];
                            //        else if ((px >= 0) && (px >= width) && (py >= 0) && (py < height)) // → 右はみ出し
                            //            pixel = inData[(px - width) + py * inWidth];
                            //        else if ((px >= 0) && (px < width) && (py >= 0) && (py >= height)) // ↓ 下はみ出し
                            //            pixel = inData[px + (py - height) * inWidth];
                            //        else if ((px >= 0) && (px >= width) && (py >= 0) && (py >= height)) // ↘ 右下はみ出し
                            //            pixel = inData[(px - width) + (py - height)  * inWidth];
                            //        else if ((px < 0) && (px < width) && (py >= 0) && (py < height)) // ← 左はみ出し
                            //            pixel = inData[x + py * inWidth];
                            //        else if ((px >= 0) && (px < width) && (py < 0) && (py < height)) // ↑ 上はみ出し
                            //            pixel = inData[px + y * inWidth];
                            //        else if ((px < 0) && (px < width) && (py < 0) && (py < height)) // ↖ 左上はみ出し
                            //            pixel = inData[x + y * inWidth];
                            //        break;
                            //}
                            outData[setx + sety * outWidth] = pixel;
                        } else { // 8bit
                            int x, y;
                            x = px;
                            y = py;
                            PF_Pixel pixel;

                            PF_Pixel *outData = (PF_Pixel *)output->data;
                            PF_Pixel *inData = (PF_Pixel *)input->data;

                            if ((px >= 0) && (px < width) && (py >= 0) && (py < height)) // 通常
                                pixel = pGet(inData, px, py, inWidth);
                            else
                                switch (niP.border)
                            {
                                    case 1: // None
                                        pixel.alpha = 0;
                                        break;
                                    case 2: // Expand
                                        if ((px >= 0) && (px >= width)) // → 右はみ出し
                                            x = width - 1;
                                        if ((py >= 0) && (py >= height)) // ↓ 下はみ出し
                                            y = height - 1;
                                        if ((px < 0) && (px < width)) // ← 左はみ出し
                                            x = 0;
                                        if ((py < 0) && (py < height)) // ↑ 上はみ出し
                                            y = 0;
                                        pixel = pGet(inData, x, y, inWidth);
                                        break;
                                    case 3: // Mirror
                                        if ((px >= 0) && (px >= width)) // → 右はみ出し
                                            x = width - (px - width) - 1;
                                        if ((py >= 0) && (py >= height)) // ↓ 下はみ出し
                                            y = height - (py - height) - 1;
                                        if ((px < 0) && (px < width)) // ← 左はみ出し
                                            x = 0;
                                        if ((py < 0) && (py < height)) // ↑ 上はみ出し
                                            y = 0;
                                        pixel = pGet(inData, x, y, inWidth);
                                        break;
                                    case 4: // Tile
                                        if ((px >= 0) && (px >= width)) // → 右はみ出し
                                            x = px - width;
                                        if ((py >= 0) && (py >= height)) // ↓ 下はみ出し
                                            y = py - height;
                                        if ((px < 0) && (px < width)) // ← 左はみ出し
                                            x = 0;
                                        if ((py < 0) && (py < height)) // ↑ 上はみ出し
                                            y = 0;
                                        pixel = pGet(inData, x, y, inWidth);
                                        break;
                            }
                            pSet(outData, setx, sety, outWidth, pixel);
                        }
                    }
                }
            }
        }
    }

    return err;
}

DllExport
PF_Err EntryPointFunc(PF_Cmd cmd, PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output, void *extra)
{
    PF_Err		err = PF_Err_NONE;

    try {
        switch (cmd) {
            case PF_Cmd_ABOUT:
                err = About(in_data, out_data, params, output);
                break;
            case PF_Cmd_GLOBAL_SETUP:
                err = GlobalSetup(in_data, out_data, params, output);
                break;
            case PF_Cmd_PARAMS_SETUP:
                err = ParamsSetup(in_data, out_data, params, output);
                break;
            case PF_Cmd_RENDER:
                err = Render(in_data, out_data, params, output);
                break;
        }
    } catch (PF_Err &thrown_err){
        err = thrown_err;
    }
    return err;
}
