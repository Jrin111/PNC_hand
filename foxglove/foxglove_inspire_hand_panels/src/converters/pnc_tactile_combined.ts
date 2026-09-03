import type { ExtensionContext, Immutable, MessageEvent } from "@foxglove/extension";
import { NAMES_TOPIC, VALUES_TOPIC, OUTPUT_TOPIC, TactileDecoder, unknownFrame, type TactileFrame } from "../data/pnc_tactile";
import { PNC_TACTILE_SCHEMA, PNC_TACTILE_SCHEMA_NAME } from "../schemas/messages";

export function registerPncTactileCombined(extensionContext: ExtensionContext): void {
  extensionContext.registerMessageConverter({
    type: "topic",
    inputTopics: [NAMES_TOPIC, VALUES_TOPIC],
    outputTopic: OUTPUT_TOPIC,
    outputSchemaName: PNC_TACTILE_SCHEMA_NAME,
    outputSchemaDescription: PNC_TACTILE_SCHEMA,
    create: () => {
      const decoder = new TactileDecoder();
      let previousTime = -Infinity;
      return (event: Immutable<MessageEvent>): TactileFrame | undefined => {
        const now = event.receiveTime.sec + event.receiveTime.nsec / 1e9;
        if (now < previousTime) decoder.reset();
        previousTime = now;
        if (event.topic === NAMES_TOPIC) {
          decoder.setNames(event.message);
          // New mapping invalidates old values; wait for the next values message.
          return unknownFrame();
        }
        return event.topic === VALUES_TOPIC ? decoder.decode(event.message) : undefined;
      };
    },
  });
}
