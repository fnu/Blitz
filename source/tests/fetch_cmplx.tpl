complex fetch example
{{ BEGIN root }}
this is root
{{ BEGIN a }}
A-begin({{ $i }}) {{ BEGIN b }} B-begin; y={{$y}} B-end{{ END }} ; x={{$x}}, y={{$y}} A-end
{{ END }}
end of root
{{ END }}
