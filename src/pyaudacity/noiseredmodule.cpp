#include <Python.h>
#include <memory>

#include "ExportPCM.h"
#include "Mix.h"
#include "DirManager.h"
#include "ImportPCM.h"

#define PYTHON_AUDACITY_NOISERED_MODULE


static bool
PyAudacity_Noisered(const char *profile_path, double profile_start, double profile_end,
                    const char *src_path, double noise_gain, double sensitivity, double smoothing,
                    const char *dst_path) {
    // import audio file for profile
    const auto dir_manager = std::make_shared<DirManager>();
    auto factory = new TrackFactory(dir_manager);
    auto profile_handler = PCMImportFileHandle::Open(profile_path);
    TrackHolders profile_holders{};
    auto import_result = profile_handler->Import(factory, profile_holders);
    if (import_result != ProgressResult::Success) {
        delete factory;
        return false;
    }

    // get profile
    auto effect = new EffectNoiseReduction();
    auto profile_result = effect->GetProfile(profile_holders[0].get(), profile_start, profile_end,
                                             noise_gain, sensitivity, smoothing, factory);
    if (!profile_result) {
        delete factory;
        delete effect;
        return false;
    }

    // import src file
    TrackHolders src_holders{};
    auto src_handler = PCMImportFileHandle::Open(src_path);
    import_result = src_handler->Import(factory, src_holders);
    if (import_result != ProgressResult::Success) {
        delete factory;
        delete effect;
        return false;
    }
    // execute noise reduction
    auto noisered_result = effect->ReduceNoise(src_holders[0].get(),
                                               noise_gain, sensitivity, smoothing, factory);
    if (!noisered_result) {
        delete factory;
        delete effect;
        return false;
    }

    // export
    auto exporter = ExportPCM();
    auto audioArray = WaveTrackConstArray();
    audioArray.emplace_back(std::move(src_holders.at(0)));
    auto export_result = exporter.Export(audioArray, std::string(dst_path));
    if (export_result != ProgressResult::Success) {
        delete factory;
        delete effect;
        return false;
    }

    delete factory;
    delete effect;
    return true;
}

static PyObject *
pyaudacity_noisered(PyObject *self, PyObject *args) {
    const char *profile_path;
    double profile_start;
    double profile_end;
    const char *src_path;
    double noise_gain;
    double sensitivity;
    double smoothing;
    const char *dst_path;

    // parse args
    if (!PyArg_ParseTuple(args, "sddsddds",
                          &profile_path, &profile_start, &profile_end,
                          &src_path, &noise_gain, &sensitivity, &smoothing,
                          &dst_path)) {
        return Py_False;
    }

    auto result = PyAudacity_Noisered(profile_path, profile_start, profile_end,
                                      src_path, noise_gain, sensitivity, smoothing,
                                      dst_path);
    if (result) {
        return Py_True;
    } else {
        return Py_False;
    }
}

static PyMethodDef NoiseredMethods[] = {
        {"noisered", pyaudacity_noisered, METH_VARARGS, "noise reduction."},
        {nullptr,    nullptr, 0,                        nullptr}        /* Sentinel */
};

static struct PyModuleDef noiseredmodule = {
        PyModuleDef_HEAD_INIT,
        "cmodule",   /* name of module */
        nullptr, /* module documentation, may be NULL */
        -1,       /* size of per-interpreter state of the module,
                 or -1 if the module keeps state in global variables. */
        NoiseredMethods,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
};

PyMODINIT_FUNC
PyInit_cmodule(void) {
    return PyModule_Create(&noiseredmodule);
}
