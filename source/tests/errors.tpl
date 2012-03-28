{{ $hi }}                   /* OK */
    {{ ''s  }}              /* ERROR */
  {{ $ss }}                 /* OK */
{{ .lskl }}                 /* ERROR */
  {{ -+++ }}                /* ERROR */ 
    {{ x; }}                /* OK */
}}                          /* ERROR */
{{                          /* ERROR */
      {{ x(); }}            /* OK */
    {{ # a }}               /* ERROR */
  {{ END }}                 /* ERROR */
{{ if(1,2,3,4) }}           /* ERROR */
  {{ include('a','b') }}    /* ERROR */
{{  BEGIN hello }}          /* OK */
    {{ -hello }}            /* OK */
{{ END }}                   /* OK */
