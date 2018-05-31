declare _xraycli_num_services
declare _xraycli_paths

_xray_cli_complete()
{
        cur_word="${COMP_WORDS[COMP_CWORD]}"
        services=$(find /tmp/xray -maxdepth 1 -type s -printf "/%f\n" 2>/dev/null)
        num_services="$(wc -l <<< ${services})"
        if [[ "${_xray_num_services}" != "${num_services}" ]]; then
                _xraycli_paths=''
                _xray_num_services="${num_services}"
                while read -r service; do
                        echo getting options for ${service}
                        while read -r table; do
                                _xraycli_paths="${_xraycli_paths} ${service}${table}"
                        done <<< "$(xraycli ${service} | tail -n +3)"
                done <<< "${services}"
        fi
        COMPREPLY=( $(compgen -W "${_xraycli_paths}" -- ${cur_word}) )
        return 0
}
complete -F _xray_cli_complete xraycli
