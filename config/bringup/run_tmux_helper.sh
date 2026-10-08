#!/usr/bin/env bash
# Start EVerest in tmux: manager + standalone modules from config.
#
# Usage:
#   ./run_tmux_helper.sh <config.yaml> <everest_install_prefix>
#
# Example:
#   ./run_tmux_helper.sh config/bringup/config-bringup-infyACDC.yaml build/dist

set -eo pipefail

function parse_yaml {
   local prefix="${2-}"
   local s='[[:space:]]*' w='[a-zA-Z0-9_]*' fs=$(echo @|tr @ '\034')
   sed -ne "s|^\($s\):|\1|" \
        -e "s|^\($s\)\($w\)$s:$s[\"']\(.*\)[\"']$s\$|\1$fs\2$fs\3|p" \
        -e "s|^\($s\)\($w\)$s:$s\(.*\)$s\$|\1$fs\2$fs\3|p"  "$1" |
   awk -F$fs '{
      indent = length($1)/2;
      vname[indent] = $2;
      for (i in vname) {if (i > indent) {delete vname[i]}}
      if (length($3) > 0) {
         vn=""; for (i=0; i<indent; i++) {vn=(vn)(vname[i])("_")}
         printf("%s%s%s=\"%s\"\n", "'$prefix'",vn, $2, $3);
      }
   }'
}

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <config.yaml> <everest_install_prefix>"
    exit 1
fi

EVEREST_CONFIG_FILE="$1"
PREFIX="$2"

if [[ ! -f "$EVEREST_CONFIG_FILE" ]]; then
    echo "Config not found: $EVEREST_CONFIG_FILE" >&2
    exit 1
fi

if [[ ! -x "$PREFIX/bin/manager" ]]; then
    echo "manager not found: $PREFIX/bin/manager" >&2
    exit 1
fi

EVEREST_CONFIG_FILE="$(cd "$(dirname "$EVEREST_CONFIG_FILE")" && pwd)/$(basename "$EVEREST_CONFIG_FILE")"
PREFIX="$(cd "$PREFIX" && pwd)"

CONFIG_BASENAME="$(basename "$EVEREST_CONFIG_FILE" .yaml)"
SESSION_NAME="EVerest-${CONFIG_BASENAME}"
SESSION_NAME="${SESSION_NAME//[!a-zA-Z0-9_-]/_}"

eval "$(parse_yaml "$EVEREST_CONFIG_FILE" "cfg_")"
if [[ -n "${cfg_settings_mqtt_everest_prefix:-}" ]]; then
    MQTT_PREFIX="${cfg_settings_mqtt_everest_prefix}"
else
    MQTT_SUFFIX="${CONFIG_BASENAME//[!a-zA-Z0-9_-]/_}"
    MQTT_PREFIX="everest_${MQTT_SUFFIX}"
fi

LOG_CONFIG="$PREFIX/etc/everest/default_logging.cfg"
if [[ ! -f "$LOG_CONFIG" ]]; then
    LOG_CONFIG="$PREFIX/share/everest/default_logging.cfg"
fi

if tmux has-session -t "$SESSION_NAME" 2>/dev/null; then
    echo "tmux session already exists: $SESSION_NAME" >&2
    echo "  attach: tmux attach -t $SESSION_NAME" >&2
    echo "  kill:   tmux kill-session -t $SESSION_NAME" >&2
    exit 1
fi

IDS=()
NAMES=()

# Standalone discovery uses unprefixed parse_yaml keys (active_modules_<id>_standalone).
mapfile -t STANDALONE_LINES < <(parse_yaml "$EVEREST_CONFIG_FILE" | grep 'standalone="true"' || true)

PAT="_standalone=\"true\""
PAT2="active_modules_"

for LINE in "${STANDALONE_LINES[@]}"; do
  MODULE_ID=$(echo "$LINE" | sed -n "/$PAT/s/$PAT//p" | sed -n "/$PAT2/s/$PAT2//p")
  PAT3="${PAT2}${MODULE_ID}_module="
  MODULE_NAME=$(parse_yaml "$EVEREST_CONFIG_FILE" | grep "$PAT3" | sed -n "/$PAT3/s/$PAT3//p" | sed -r 's/["]+//g' || true)
  if [[ -n "$MODULE_ID" && -n "$MODULE_NAME" ]]; then
    IDS+=( "$MODULE_ID" )
    NAMES+=( "$MODULE_NAME" )
  fi
done

echo "tmux session:   $SESSION_NAME"
echo "MQTT prefix:    ${MQTT_PREFIX}/"
echo "Install prefix: $PREFIX"
echo "Config:         $EVEREST_CONFIG_FILE"
echo "Standalone:     ${#IDS[@]} module(s): ${IDS[*]:-(none)}"

tmux new-session -d -s "$SESSION_NAME"
tmux set-window-option -t "${SESSION_NAME}:0" mouse on

tmux send-keys -t "${SESSION_NAME}:0.0" \
    "$PREFIX/bin/manager --prefix $PREFIX --config $EVEREST_CONFIG_FILE --mqtt_everest_prefix $MQTT_PREFIX" C-m

LEN=${#NAMES[@]}

# Layout: manager left (pane 0), standalone BU modules stacked on the right.
# First split -h (vertical divider), further splits -v on the right column (horizontal dividers).
for (( j=0; j<LEN; j++ )); do
  if [[ $j -gt 0 ]]; then
    # Stack CLIs in the right column; -p 50 keeps each new pane half of that column.
    tmux split-window -t "${SESSION_NAME}:0.${j}" -v -p 50
  else
    # -h: vertical divider (manager left, CLIs right). -p 50: equal width, not main-vertical's ~2/3.
    tmux split-window -t "${SESSION_NAME}:0.0" -h -p 50
  fi
  MODULE_BIN="$PREFIX/libexec/everest/modules/${NAMES[$j]}/${NAMES[$j]}"
  tmux send-keys -t "${SESSION_NAME}:0.$((j+1))" \
    "sleep 1 && $MODULE_BIN --prefix $PREFIX --module ${IDS[$j]} --log_config $LOG_CONFIG --mqtt_everest_prefix $MQTT_PREFIX" C-m
done

if [[ $LEN -gt 0 ]]; then
  tmux select-pane -t "${SESSION_NAME}:0.0"
fi

tmux attach-session -t "$SESSION_NAME"
tmux kill-session -t "$SESSION_NAME"
