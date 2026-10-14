#!/usr/bin/env python3
"""Guarantee a blocking tick even when display notifications are continuously pending."""
import argparse
from pathlib import Path

OLD = '        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(task_delay_ms));'
NEW = '''        /* A pending flush notification makes NotifyTake return immediately.
         * Block outside the LVGL mutex so IDLE and UI runtime always get time. */
        vTaskDelay(1);
''' + OLD

def patch(source):
    if source.count(NEW) == 1 and source.replace(NEW, '').count(OLD) == 0:
        return source
    if source.count(OLD) != 1:
        raise ValueError('Unsupported adapter worker wait; expected 0.6.4 scheduler')
    return source.replace(OLD, NEW)

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--input',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    fixed=patch(args.input.read_text())
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(fixed)
    print('LVGL worker guarantees one blocking tick outside the display lock')
