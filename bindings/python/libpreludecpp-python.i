%include std_list.i
%template() std::list<Prelude::IDMEFValue>;
%template() std::list<Prelude::Connection>;

%rename (__str__) *::operator const std::string() const;
%rename (__str__) *::operator const char *() const;
%rename (__int__) *::operator int() const;
%rename (__long__) *::operator long() const;
%rename (__float__) *::operator double() const;

%ignore *::operator =;

%header %{
#define TARGET_LANGUAGE_OUTPUT_TYPE PyObject **
int IDMEFValue_to_SWIG(const IDMEFValue &result, TARGET_LANGUAGE_OUTPUT_TYPE ret);
%}


%{
static int _cb_python_write(prelude_msgbuf_t *fd, prelude_msg_t *msg)
{
        size_t ret;
        PyObject *io = (PyObject *) prelude_msgbuf_get_data(fd);
        FILE *f = PyFile_AsFile(io);

        ret = fwrite((const char *)prelude_msg_get_message_data(msg), 1, prelude_msg_get_len(msg), f);
        if ( ret != prelude_msg_get_len(msg) )
                return prelude_error_from_errno(errno);

        prelude_msg_recycle(msg);

        return 0;
}


static ssize_t _cb_python_read(prelude_io_t *fd, void *buf, size_t size)
{
        ssize_t ret;
        PyObject *io = (PyObject *) prelude_io_get_fdptr(fd);
        FILE *f = PyFile_AsFile(io);

        ret = fread(buf, 1, size, f);
        if ( ret < 0 )
                ret = prelude_error_from_errno(errno);

        else if ( ret == 0 )
                ret = prelude_error(PRELUDE_ERROR_EOF);

        return ret;
}
%}


/* tell squid not to cast void * value */
%typemap(in) void *nocast_file_p {
        if ( !PyFile_Check( (PyObject *)$input) ) {
                const char * errstr = "Argument is not a file object.";
                PyErr_SetString(PyExc_RuntimeError,errstr);
                return NULL;
        }
        $1 = $input;
}


%extend Prelude::IDMEF {
        void Write(void *nocast_file_p) {
                self->_genericWrite(_cb_python_write, nocast_file_p);
        }

        void Read(void *nocast_file_p) {
                self->_genericRead(_cb_python_read, nocast_file_p);
        }

        Prelude::IDMEF &operator >> (PyObject *o) {
                self->_genericWrite(_cb_python_write, o);
                return *self;
        }

        Prelude::IDMEF &operator << (PyObject *o) {
                self->_genericRead(_cb_python_read, o);
                return *self;
        }
}

%fragment("IDMEFValueList_to_SWIG", "header") {
PyObject *IDMEFValueList_to_SWIG(const Prelude::IDMEFValue &value)
{
        int j = 0, ret;
        PyObject *pytuple;
        std::list<Prelude::IDMEFValue> result = value;
        std::list<Prelude::IDMEFValue>::const_iterator i;

        pytuple = PyTuple_New(result.size());

        for ( i = result.begin(); i != result.end(); i++ ) {
                PyObject *val;

                ret = IDMEFValue_to_SWIG(*i, &val);
                if ( ret < 0 )
                        return NULL;

                PyTuple_SetItem(pytuple, j++, val);
        }

        return pytuple;
}
}


%typemap(out, fragment="IDMEFValue_to_SWIG") Prelude::IDMEFValue {
        int ret;

        if ( $1.IsNull() ) {
                Py_INCREF(Py_None);
                $result = Py_None;
        } else {
                ret = IDMEFValue_to_SWIG($1, &$result);
                if ( ret < 0 ) {
                        std::stringstream s;
                        s << "IDMEFValue typemap does not handle value of type '" << idmef_value_type_to_string($1.GetType()) << "'";
                        SWIG_exception_fail(SWIG_ValueError, s.str().c_str());
                }
        }
};


%init {
        int argc, ret, i;
        char **argv = NULL;
        PyObject *sys = PyImport_ImportModule("sys");
        PyObject *pyargv = PyObject_GetAttrString(sys, "argv");

        argc = PyObject_Length(pyargv);
        assert(argc >= 1);
        assert(PyList_Check(pyargv));

        if ( argc + 1 < 0 )
                throw PreludeError("Invalid argc length");

        argv = (char **) malloc((argc + 1) * sizeof(char *));
        if ( ! argv )
                throw PreludeError("Allocation failure");

        for ( i = 0; i < argc; i++ ) {
                PyObject *o = PyList_GetItem(pyargv, i);
                argv[i] = PyString_AsString(o);
        }

        argv[i] = NULL;

        ret = prelude_init(&argc, argv);
        if ( ret < 0 ) {
                free(argv);
                throw PreludeError(ret);
        }

        Py_DECREF(pyargv);
        Py_DECREF(sys);
}