package com.dudleylane.quickfix.test;

import com.dudleylane.quickfix.*;

public class DataDictionaryTests {
    public void testLookups() {
        DataDictionary dictionary = new DataDictionary(Fixture.spec("FIX42"));
        Check.isTrue(dictionary.isField(55), "Symbol is a field");
        Check.isTrue(dictionary.isMsgType("D"), "NewOrderSingle is a message");
        Check.isTrue(dictionary.isRequiredField("D", 11), "ClOrdID is required in it");
        Check.isTrue(dictionary.isHeaderField(49), "SenderCompID is a header field");
        Check.isTrue(dictionary.isGroup("V", 146), "NoRelatedSym is a group in MarketDataRequest");
    }

    public void testValidationFailuresAreTheEnginesTypes() {
        DataDictionary dictionary = new DataDictionary(Fixture.spec("FIX42"));
        Message message = new Message(MessageTests.wire());
        dictionary.validate(message);
        message.removeField(11);
        RequiredTagMissing missing = Check.raises(RequiredTagMissing.class, () -> dictionary.validate(message));
        Check.equal(11, missing.getField(), "names ClOrdID");
        Message extra = new Message(MessageTests.wire());
        extra.setField(146, "1");
        TagNotDefinedForMessage notDefined = Check.raises(TagNotDefinedForMessage.class, () -> dictionary.validate(extra));
        Check.equal(146, notDefined.getField(), "names NoRelatedSym");
    }

    public void testAMissingFileIsConfigError() {
        Check.raises(ConfigError.class, () -> new DataDictionary("/no/such/FIX42.xml"));
    }
}
