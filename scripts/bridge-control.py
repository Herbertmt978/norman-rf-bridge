"""Commission or operate one explicit ESPHome RF prototype over its native API.

Templates and rolling state are per-installation data, never compiled into the
factory image. Optional API encryption key is read from ESPHOME_NOISE_PSK only.
"""
import argparse
import asyncio
from dataclasses import asdict
import json
import os

import aioesphomeapi


async def main(args):
    client = aioesphomeapi.APIClient(
        args.host, 6053, '', expected_name=args.expected_name,
        noise_psk=os.environ.get('ESPHOME_NOISE_PSK'),
    )
    await client.connect(login=True)
    try:
        _, services = await client.list_entities_services()
        action, data = {
            'status': ('rf_panel_status', {}),
            'targets': ('rf_targets_status', {}),
            'relay-endpoints': ('rf_relay_endpoints_status', {}),
            'learn-relay-endpoint': ('rf_configure_relay_endpoint', {
                'slot': args.slot, 'name': args.name, 'frame': args.frame,
            }),
            'target-command': ('rf_target_command', {'slot': args.slot, 'position': args.position, 'profile_id': args.profile_id}),
            'learn-target-endpoint': ('rf_learn_target_endpoint', {'slot': args.slot, 'position': args.position, 'frame': args.frame}),
            'commission-target': ('rf_configure_target', {
                'slot': args.slot, 'name': args.name, 'room': args.room,
                'open_frame': args.open_frame, 'close_frame': args.close_down_frame,
                'last_index': args.last_index, 'open_position': args.open_position,
                'close_position': args.close_position,
            }),
            'commission': ('rf_configure_panel', {
                'open_frame': args.open_frame, 'close_down_frame': args.close_down_frame,
                'last_index': args.last_index, 'open_position': args.open_position,
            }),
            'command': ('rf_panel_command', {'position': args.position}),
            'relay': ('rf_set_relay', {'enabled': args.enabled == 'on'}),
            'learn-close-up': ('rf_learn_close_up', {'frame': args.frame}),
        }[args.operation]
        service = next(item for item in services if item.name == action)
        response = await client.execute_service(
            service, data, return_response=args.operation in ('status', 'targets', 'relay-endpoints'), timeout=12,
        )
        if response is None:
            raise RuntimeError('No firmware acknowledgement received')
        result = asdict(response)
        result['response_data'] = json.loads(response.response_data) if response.response_data else None
        print(json.dumps(result))
        if not response.success:
            raise RuntimeError('Firmware rejected the operation; see response above')
    finally:
        await client.disconnect()


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host', required=True)
parser.add_argument('--expected-name', required=True)
parser.add_argument('operation', choices=['status', 'targets', 'relay-endpoints', 'learn-relay-endpoint', 'commission', 'commission-target', 'command', 'target-command', 'relay', 'learn-close-up', 'learn-target-endpoint'])
parser.add_argument('--slot', type=int, choices=range(32))
parser.add_argument('--profile-id')
parser.add_argument('--name')
parser.add_argument('--room')
parser.add_argument('--close-position', type=int, choices=[0, 100], default=0)
parser.add_argument('--open-frame')
parser.add_argument('--close-frame', '--close-down-frame', dest='close_down_frame')
parser.add_argument('--frame')
parser.add_argument('--last-index', type=int, choices=range(256))
parser.add_argument('--open-position', type=int, default=37)
parser.add_argument('--position', type=int, choices=range(101))
parser.add_argument('--enabled', choices=['on', 'off'])
args = parser.parse_args()
required = {'commission': ['open_frame', 'close_down_frame', 'last_index'],
            'commission-target': ['slot', 'name', 'room', 'open_frame', 'close_down_frame', 'last_index'],
            'target-command': ['slot', 'position', 'profile_id'],
            'learn-target-endpoint': ['slot', 'position', 'frame'],
            'learn-relay-endpoint': ['slot', 'name', 'frame'],
            'command': ['position'], 'relay': ['enabled'], 'learn-close-up': ['frame']}
if any(getattr(args, name) is None for name in required.get(args.operation, [])):
    parser.error('Missing operation-specific arguments')
asyncio.run(main(args))
